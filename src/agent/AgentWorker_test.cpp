// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>
#include <vector>

#include <agent/AgentWorker.hpp>

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
