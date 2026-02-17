// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <agent/AgentSession.hpp>

using namespace endo::agent;

namespace
{

/// Mock LLM provider for testing AgentSession without network calls.
class MockProvider final: public LlmProvider
{
  public:
    std::string responseText = "Mock response";
    bool shouldFail = false;
    ProviderError failError { ProviderErrorCode::NetworkError, "connection failed", 500 };

    [[nodiscard]] auto generate(std::span<ChatMessage const> messages,
                                std::span<ToolDefinition const> /*tools*/,
                                StreamCallback streamCb)
        -> std::expected<GenerateResult, ProviderError> override
    {
        lastMessages.assign(messages.begin(), messages.end());

        if (shouldFail)
            return std::unexpected(failError);

        // Stream tokens if callback provided
        if (streamCb)
        {
            streamCb(responseText);
        }

        auto result = GenerateResult {};
        result.content.emplace_back(TextBlock { .text = responseText });
        return result;
    }

    [[nodiscard]] auto supportsToolUse() const noexcept -> bool override { return false; }

    [[nodiscard]] auto supportsImageInput() const noexcept -> bool override { return false; }

    [[nodiscard]] auto supportsImageOutput() const noexcept -> bool override { return false; }

    [[nodiscard]] auto contextSize() const noexcept -> size_t override { return 8192; }

    [[nodiscard]] auto modelInfo() const -> ModelInfo override
    {
        return ModelInfo { .providerName = "mock", .modelName = "mock-1" };
    }

    std::vector<ChatMessage> lastMessages;
};

} // namespace

TEST_CASE("AgentSession.process_message_returns_response", "[agent]")
{
    auto provider = MockProvider {};
    provider.responseText = "Hello! How can I help?";
    auto session = AgentSession(provider);

    auto result = session.processMessage("Hi there", nullptr);

    REQUIRE(result.has_value());
    CHECK(*result == "Hello! How can I help?");
}

TEST_CASE("AgentSession.adds_messages_to_history", "[agent]")
{
    auto provider = MockProvider {};
    provider.responseText = "Response 1";
    auto session = AgentSession(provider);

    auto result = session.processMessage("Question 1", nullptr);
    REQUIRE(result.has_value());

    CHECK(session.history().size() == 2);
    CHECK(session.history().messages()[0].role == Role::User);
    CHECK(session.history().messages()[0].textContent() == "Question 1");
    CHECK(session.history().messages()[1].role == Role::Assistant);
    CHECK(session.history().messages()[1].textContent() == "Response 1");
}

TEST_CASE("AgentSession.preserves_history_across_calls", "[agent]")
{
    auto provider = MockProvider {};
    auto session = AgentSession(provider);

    provider.responseText = "First response";
    (void) session.processMessage("First question", nullptr);

    provider.responseText = "Second response";
    (void) session.processMessage("Second question", nullptr);

    CHECK(session.history().size() == 4);
    // Provider receives conversation history at time of generate() call
    // (before the second assistant response is appended)
    CHECK(provider.lastMessages.size() == 3);
}

TEST_CASE("AgentSession.streaming_callback_called", "[agent]")
{
    auto provider = MockProvider {};
    provider.responseText = "Streamed token";
    auto session = AgentSession(provider);

    auto receivedTokens = std::string {};
    auto result = session.processMessage("Hello", [&](std::string_view token) { receivedTokens += token; });

    REQUIRE(result.has_value());
    CHECK(receivedTokens == "Streamed token");
}

TEST_CASE("AgentSession.error_propagation", "[agent]")
{
    auto provider = MockProvider {};
    provider.shouldFail = true;
    auto session = AgentSession(provider);

    auto result = session.processMessage("Hello", nullptr);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == AgentErrorCode::ProviderError);
    CHECK(result.error().message.find("connection failed") != std::string::npos);
}

TEST_CASE("AgentSession.system_prompt", "[agent]")
{
    auto provider = MockProvider {};
    provider.responseText = "OK";
    auto session = AgentSession(provider);

    session.setSystemPrompt("You are helpful.");
    (void) session.processMessage("Hello", nullptr);

    // System prompt should be first in history
    CHECK(session.history().messages()[0].role == Role::System);
    CHECK(session.history().messages()[0].textContent() == "You are helpful.");
    // Provider should also receive the system prompt
    CHECK(provider.lastMessages[0].role == Role::System);
}

TEST_CASE("AgentSession.reset_clears_history", "[agent]")
{
    auto provider = MockProvider {};
    provider.responseText = "Response";
    auto session = AgentSession(provider);

    (void) session.processMessage("Hello", nullptr);
    CHECK_FALSE(session.history().empty());

    session.reset();
    CHECK(session.history().empty());
}
