// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>
#include <vector>

#include <agent/session/AgentWorker.hpp>
#include <agent/tools/AskUserTool.hpp>
#include <agent/tools/ToolRegistry.hpp>

using namespace endo::agent;

namespace
{

/// Mock LLM provider for testing AgentWorker without network calls.
class MockProvider final: public LlmProvider
{
  public:
    std::string responseText = "Mock response";
    bool shouldFail = false;
    ProviderError failError { .code = ProviderErrorCode::NetworkError,
                              .message = "connection failed",
                              .httpStatus = 500 };
    std::atomic<int> generateCallCount { 0 };
    std::chrono::milliseconds artificialDelay { 0 };

    [[nodiscard]] auto generate(std::span<ChatMessage const> /*messages*/,
                                std::span<ToolDefinition const> /*tools*/,
                                StreamCallback streamCb)
        -> std::expected<GenerateResult, ProviderError> override
    {
        ++generateCallCount;

        if (artificialDelay.count() > 0)
            std::this_thread::sleep_for(artificialDelay);

        if (shouldFail)
            return std::unexpected(failError);

        // Stream tokens character by character.
        if (streamCb)
        {
            for (auto c: responseText)
            {
                if (!streamCb(std::string_view(&c, 1)))
                    return std::unexpected(
                        ProviderError { .code = ProviderErrorCode::Unknown, .message = "cancelled" });
            }
        }

        auto result = GenerateResult {};
        result.content.emplace_back(TextBlock { .text = responseText });
        return result;
    }

    [[nodiscard]] auto supportsToolUse() const noexcept -> bool override { return true; }

    [[nodiscard]] auto supportsImageInput() const noexcept -> bool override { return false; }

    [[nodiscard]] auto supportsImageOutput() const noexcept -> bool override { return false; }

    [[nodiscard]] auto contextSize() const noexcept -> size_t override { return 100000; }

    [[nodiscard]] auto modelInfo() const -> ModelInfo override
    {
        return { .providerName = "mock", .modelName = "mock-v1" };
    }
};

} // namespace

TEST_CASE("AgentWorker.prompt_produces_tokens_and_completion", "[agent][worker]")
{
    auto outbound = endo::platform::MessageQueue<FromAgentMessage> {};
    auto provider = MockProvider {};
    provider.responseText = "Hello!";

    auto session = AgentSession(provider);
    session.setSystemPrompt("You are a test agent.");

    auto worker = AgentWorker(session, outbound);
    worker.start();

    worker.inbound().push(UserPromptMessage { .text = "Hi" });

    // Collect messages until we get a CompletionMessage.
    auto tokens = std::string {};
    auto gotThinking = false;
    auto gotCompletion = false;

    auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline && !gotCompletion)
    {
        auto msg = outbound.popFor(std::chrono::milliseconds(100));
        if (!msg.has_value())
            continue;
        std::visit(
            [&](auto const& m) {
                using T = std::decay_t<decltype(m)>;
                if constexpr (std::is_same_v<T, ThinkingStartMessage>)
                    gotThinking = true;
                else if constexpr (std::is_same_v<T, TokenMessage>)
                    tokens += m.token;
                else if constexpr (std::is_same_v<T, CompletionMessage>)
                    gotCompletion = true;
            },
            *msg);
    }

    CHECK(gotThinking);
    CHECK(tokens == "Hello!");
    CHECK(gotCompletion);

    worker.stop();
}

TEST_CASE("AgentWorker.cancellation_stops_streaming", "[agent][worker]")
{
    auto outbound = endo::platform::MessageQueue<FromAgentMessage> {};
    auto provider = MockProvider {};
    provider.responseText = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    provider.artificialDelay = std::chrono::milliseconds(50);

    auto session = AgentSession(provider);
    session.setSystemPrompt("Test");

    auto worker = AgentWorker(session, outbound);
    worker.start();

    worker.inbound().push(UserPromptMessage { .text = "Go" });

    // Wait for thinking to start, then cancel.
    auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline)
    {
        auto msg = outbound.popFor(std::chrono::milliseconds(50));
        if (msg.has_value() && std::holds_alternative<ThinkingStartMessage>(*msg))
        {
            worker.inbound().push(CancelMessage {});
            break;
        }
    }

    // Wait for completion (which should come quickly after cancel).
    auto gotCompletion = false;
    while (std::chrono::steady_clock::now() < deadline && !gotCompletion)
    {
        auto msg = outbound.popFor(std::chrono::milliseconds(100));
        if (msg.has_value() && std::holds_alternative<CompletionMessage>(*msg))
            gotCompletion = true;
    }

    CHECK(gotCompletion);
    worker.stop();
}

TEST_CASE("AgentWorker.shutdown_during_idle", "[agent][worker]")
{
    auto outbound = endo::platform::MessageQueue<FromAgentMessage> {};
    auto provider = MockProvider {};

    auto session = AgentSession(provider);

    auto worker = AgentWorker(session, outbound);
    worker.start();

    // Immediate shutdown without sending any prompts.
    worker.stop();

    // Should get shutdown complete message.
    auto msg = outbound.popFor(std::chrono::milliseconds(1000));
    REQUIRE(msg.has_value());
    CHECK(std::holds_alternative<AgentShutdownComplete>(*msg));
}

TEST_CASE("AgentWorker.error_produces_failed_completion", "[agent][worker]")
{
    auto outbound = endo::platform::MessageQueue<FromAgentMessage> {};
    auto provider = MockProvider {};
    provider.shouldFail = true;

    auto session = AgentSession(provider);
    session.setSystemPrompt("Test");

    auto worker = AgentWorker(session, outbound);
    worker.start();

    worker.inbound().push(UserPromptMessage { .text = "Fail" });

    auto gotCompletion = false;
    auto errorMessage = std::string {};

    auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline && !gotCompletion)
    {
        auto msg = outbound.popFor(std::chrono::milliseconds(100));
        if (!msg.has_value())
            continue;
        if (auto const* completion = std::get_if<CompletionMessage>(&*msg))
        {
            gotCompletion = true;
            CHECK_FALSE(completion->success);
            errorMessage = completion->errorMessage;
        }
    }

    CHECK(gotCompletion);
    CHECK_FALSE(errorMessage.empty());
    worker.stop();
}

// ============================================================================
// ask_user tool integration tests
// ============================================================================

namespace
{

/// Mock provider that returns an ask_user tool call on the first generate(),
/// then a plain text response on subsequent calls.
class AskUserMockProvider final: public LlmProvider
{
  public:
    std::string finalResponseText = "Got user answer";

    [[nodiscard]] auto generate(std::span<ChatMessage const> /*messages*/,
                                std::span<ToolDefinition const> /*tools*/,
                                StreamCallback streamCb)
        -> std::expected<GenerateResult, ProviderError> override
    {
        auto const callNum = _callCount.fetch_add(1, std::memory_order_relaxed);

        if (callNum == 0)
        {
            // First call: return an ask_user tool call.
            auto result = GenerateResult {};
            result.content.emplace_back(ToolUseBlock {
                .id = "call_1", .name = "ask_user", .arguments = { { "question", "Pick a color?" } } });
            result.toolCalls.emplace_back(ToolCall {
                .id = "call_1", .name = "ask_user", .arguments = { { "question", "Pick a color?" } } });
            return result;
        }

        // Subsequent calls: return a plain text response.
        if (streamCb)
        {
            for (auto c: finalResponseText)
            {
                if (!streamCb(std::string_view(&c, 1)))
                    return std::unexpected(
                        ProviderError { .code = ProviderErrorCode::Unknown, .message = "cancelled" });
            }
        }

        auto result = GenerateResult {};
        result.content.emplace_back(TextBlock { .text = finalResponseText });
        return result;
    }

    [[nodiscard]] auto supportsToolUse() const noexcept -> bool override { return true; }

    [[nodiscard]] auto supportsImageInput() const noexcept -> bool override { return false; }

    [[nodiscard]] auto supportsImageOutput() const noexcept -> bool override { return false; }

    [[nodiscard]] auto contextSize() const noexcept -> size_t override { return 100000; }

    [[nodiscard]] auto modelInfo() const -> ModelInfo override
    {
        return { .providerName = "mock", .modelName = "ask-user-mock-v1" };
    }

  private:
    std::atomic<int> _callCount { 0 };
};

} // namespace

TEST_CASE("AgentWorker.ask_user_tool_roundtrip", "[agent][worker][ask_user]")
{
    auto outbound = endo::platform::MessageQueue<FromAgentMessage> {};
    auto provider = AskUserMockProvider {};
    provider.finalResponseText = "User chose blue";

    auto session = AgentSession(provider);
    session.setSystemPrompt("Test agent");

    auto worker = AgentWorker(session, outbound);

    // Register ask_user tool with the worker's callback.
    auto registry = ToolRegistry {};
    registry.registerTool(std::make_unique<AskUserTool>(worker.makeAskUserCallback()));
    session.setToolRegistry(&registry);

    worker.start();
    worker.inbound().push(UserPromptMessage { .text = "Help me pick a color" });

    // Collect outbound messages, respond to AskUserRequest, wait for completion.
    auto gotAskUser = false;
    auto gotCompletion = false;
    auto completionSuccess = false;

    auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline && !gotCompletion)
    {
        auto msg = outbound.popFor(std::chrono::milliseconds(100));
        if (!msg.has_value())
            continue;

        std::visit(
            [&](auto const& m) {
                using T = std::decay_t<decltype(m)>;
                if constexpr (std::is_same_v<T, AskUserRequest>)
                {
                    gotAskUser = true;
                    CHECK(m.question.text == "Pick a color?");
                    // Respond with the user's answer via inbound.
                    worker.inbound().push(UserAnswerMessage {
                        .requestId = m.requestId,
                        .answer = UserAnswer { .answer = "blue" },
                    });
                }
                else if constexpr (std::is_same_v<T, CompletionMessage>)
                {
                    gotCompletion = true;
                    completionSuccess = m.success;
                }
            },
            *msg);
    }

    CHECK(gotAskUser);
    CHECK(gotCompletion);
    CHECK(completionSuccess);

    worker.stop();
}

TEST_CASE("AgentWorker.ask_user_cancellation_during_wait", "[agent][worker][ask_user]")
{
    auto outbound = endo::platform::MessageQueue<FromAgentMessage> {};
    auto provider = AskUserMockProvider {};

    auto session = AgentSession(provider);
    session.setSystemPrompt("Test agent");

    auto worker = AgentWorker(session, outbound);

    auto registry = ToolRegistry {};
    registry.registerTool(std::make_unique<AskUserTool>(worker.makeAskUserCallback()));
    session.setToolRegistry(&registry);

    worker.start();
    worker.inbound().push(UserPromptMessage { .text = "Ask me something" });

    // Wait for the AskUserRequest, then send CancelMessage instead of answering.
    auto gotAskUser = false;
    auto gotCompletion = false;

    auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline && !gotAskUser)
    {
        auto msg = outbound.popFor(std::chrono::milliseconds(100));
        if (!msg.has_value())
            continue;
        if (std::holds_alternative<AskUserRequest>(*msg))
        {
            gotAskUser = true;
            // Send cancel instead of an answer.
            worker.inbound().push(CancelMessage {});
        }
    }

    // The worker should complete (with the ask_user returning cancelled).
    while (std::chrono::steady_clock::now() < deadline && !gotCompletion)
    {
        auto msg = outbound.popFor(std::chrono::milliseconds(100));
        if (!msg.has_value())
            continue;
        if (std::holds_alternative<CompletionMessage>(*msg))
            gotCompletion = true;
    }

    CHECK(gotAskUser);
    CHECK(gotCompletion);

    worker.stop();
}
