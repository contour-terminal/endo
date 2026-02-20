// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <agent/conversation/ConversationCompactor.hpp>
#include <agent/conversation/TokenEstimator.hpp>

using namespace endo::agent;

namespace
{

/// Mock provider for compaction tests with a small context window.
class CompactionMockProvider final: public LlmProvider
{
  public:
    size_t contextWindow = 500; // Deliberately small for testing
    std::string summaryResponse = "This is a conversation summary.";
    bool shouldFail = false;
    int generateCallCount = 0;

    [[nodiscard]] auto generate(std::span<ChatMessage const>, std::span<ToolDefinition const>, StreamCallback)
        -> std::expected<GenerateResult, ProviderError> override
    {
        ++generateCallCount;
        if (shouldFail)
            return std::unexpected(ProviderError {
                .code = ProviderErrorCode::ServerError,
                .message = "summary generation failed",
            });

        auto result = GenerateResult {};
        result.content.emplace_back(TextBlock { .text = summaryResponse });
        return result;
    }

    [[nodiscard]] auto supportsToolUse() const noexcept -> bool override { return false; }

    [[nodiscard]] auto supportsImageInput() const noexcept -> bool override { return false; }

    [[nodiscard]] auto supportsImageOutput() const noexcept -> bool override { return false; }

    [[nodiscard]] auto contextSize() const noexcept -> size_t override { return contextWindow; }

    [[nodiscard]] auto modelInfo() const -> ModelInfo override
    {
        return ModelInfo { .providerName = "mock", .modelName = "mock-compact" };
    }
};

} // namespace

TEST_CASE("ConversationCompactor.below_threshold_no_compaction", "[agent]")
{
    auto provider = CompactionMockProvider {};
    provider.contextWindow = 100000; // Very large context
    auto compactor = ConversationCompactor(provider);

    auto history = ConversationHistory {};
    history.setSystemPrompt("System prompt");
    history.addMessage(ChatMessage::text(Role::User, "Hello"));
    history.addMessage(ChatMessage::text(Role::Assistant, "Hi"));

    auto result = compactor.compactIfNeeded(history);
    REQUIRE(result.has_value());
    CHECK(*result == false);
    CHECK(history.size() == 3); // Unchanged
}

TEST_CASE("ConversationCompactor.at_threshold_triggers_compaction", "[agent]")
{
    auto provider = CompactionMockProvider {};
    provider.contextWindow = 100; // Very small context to trigger compaction
    auto config = CompactionConfig { .triggerThreshold = 0.5, .preserveLastMessages = 2 };
    auto compactor = ConversationCompactor(provider, config);

    auto history = ConversationHistory {};
    history.setSystemPrompt("System prompt for the agent session");

    // Add enough messages to exceed threshold (100 * 0.5 = 50 tokens)
    for (auto i = 0; i < 10; ++i)
    {
        history.addMessage(ChatMessage::text(Role::User, "This is a user message with some content"));
        history.addMessage(ChatMessage::text(Role::Assistant, "This is an assistant response with content"));
    }

    auto const sizeBefore = history.size();
    auto result = compactor.compactIfNeeded(history);
    REQUIRE(result.has_value());
    CHECK(*result == true);
    CHECK(history.size() < sizeBefore);
    CHECK(provider.generateCallCount == 1);
}

TEST_CASE("ConversationCompactor.system_prompt_preserved", "[agent]")
{
    auto provider = CompactionMockProvider {};
    provider.contextWindow = 100;
    auto config = CompactionConfig { .triggerThreshold = 0.1, .preserveLastMessages = 2 };
    auto compactor = ConversationCompactor(provider, config);

    auto history = ConversationHistory {};
    history.setSystemPrompt("Important system instructions");

    for (auto i = 0; i < 10; ++i)
    {
        history.addMessage(ChatMessage::text(Role::User, "Some user message with content here"));
        history.addMessage(ChatMessage::text(Role::Assistant, "Some assistant response here too"));
    }

    auto result = compactor.compactIfNeeded(history);
    REQUIRE(result.has_value());
    CHECK(*result == true);

    // System prompt should be the first message
    CHECK(history.messages()[0].role == Role::System);
    CHECK(history.messages()[0].textContent() == "Important system instructions");
}

TEST_CASE("ConversationCompactor.last_messages_preserved", "[agent]")
{
    auto provider = CompactionMockProvider {};
    provider.contextWindow = 100;
    auto config = CompactionConfig { .triggerThreshold = 0.1, .preserveLastMessages = 2 };
    auto compactor = ConversationCompactor(provider, config);

    auto history = ConversationHistory {};
    history.setSystemPrompt("System prompt");

    for (auto i = 0; i < 10; ++i)
    {
        history.addMessage(ChatMessage::text(Role::User, std::format("User message {}", i)));
        history.addMessage(ChatMessage::text(Role::Assistant, std::format("Assistant response {}", i)));
    }

    auto result = compactor.compactIfNeeded(history);
    REQUIRE(result.has_value());
    CHECK(*result == true);

    // Last 2 messages should be preserved
    auto const msgs = history.messages();
    auto const lastMsg = msgs[msgs.size() - 1];
    CHECK(lastMsg.textContent() == "Assistant response 9");
}

TEST_CASE("ConversationCompactor.tool_result_chains_preserved", "[agent]")
{
    auto provider = CompactionMockProvider {};
    provider.contextWindow = 100;
    auto config = CompactionConfig { .triggerThreshold = 0.1, .preserveLastMessages = 2 };
    auto compactor = ConversationCompactor(provider, config);

    auto history = ConversationHistory {};
    history.setSystemPrompt("System prompt");

    // Add many messages to exceed threshold
    for (auto i = 0; i < 8; ++i)
        history.addMessage(ChatMessage::text(Role::User, "Padding message with enough content"));

    // Add a tool use assistant message
    auto toolUseMsg = ChatMessage { .role = Role::Assistant };
    toolUseMsg.content.emplace_back(ToolUseBlock { .id = "call-1", .name = "read_file", .arguments = {} });
    history.addMessage(std::move(toolUseMsg));

    // Add a tool result user message
    auto toolResultMsg = ChatMessage { .role = Role::User };
    toolResultMsg.content.emplace_back(ToolResultBlock { .toolUseId = "call-1", .content = "file contents" });
    history.addMessage(std::move(toolResultMsg));

    auto result = compactor.compactIfNeeded(history);
    REQUIRE(result.has_value());
    CHECK(*result == true);

    // Tool result chain should be preserved (the tool result + its assistant message)
    auto const msgs = history.messages();
    auto foundToolResult = false;
    for (auto const& msg: msgs)
    {
        for (auto const& block: msg.content)
        {
            if (std::holds_alternative<ToolResultBlock>(block))
                foundToolResult = true;
        }
    }
    CHECK(foundToolResult);
}

TEST_CASE("ConversationCompactor.summary_failure_preserves_history", "[agent]")
{
    auto provider = CompactionMockProvider {};
    provider.contextWindow = 100;
    provider.shouldFail = true;
    auto config = CompactionConfig { .triggerThreshold = 0.1, .preserveLastMessages = 2 };
    auto compactor = ConversationCompactor(provider, config);

    auto history = ConversationHistory {};
    history.setSystemPrompt("System prompt");
    for (auto i = 0; i < 10; ++i)
        history.addMessage(ChatMessage::text(Role::User, "Padding message with content here"));

    auto const sizeBefore = history.size();

    auto result = compactor.compactIfNeeded(history);
    REQUIRE_FALSE(result.has_value());
    CHECK(history.size() == sizeBefore); // History unchanged
}

TEST_CASE("ConversationCompactor.zero_context_size_no_compaction", "[agent]")
{
    auto provider = CompactionMockProvider {};
    provider.contextWindow = 0;
    auto compactor = ConversationCompactor(provider);

    auto history = ConversationHistory {};
    history.setSystemPrompt("System prompt");
    history.addMessage(ChatMessage::text(Role::User, "Hello"));

    auto result = compactor.compactIfNeeded(history);
    REQUIRE(result.has_value());
    CHECK(*result == false);
}

TEST_CASE("ConversationCompactor.replaceMessages_recalculates_tokens", "[agent]")
{
    auto provider = CompactionMockProvider {};
    provider.contextWindow = 100;
    auto config = CompactionConfig { .triggerThreshold = 0.1, .preserveLastMessages = 2 };
    auto compactor = ConversationCompactor(provider, config);

    auto history = ConversationHistory {};
    history.setSystemPrompt("System prompt");
    for (auto i = 0; i < 10; ++i)
        history.addMessage(ChatMessage::text(Role::User, "A message with some content in it"));

    auto const tokensBefore = history.estimatedTokenCount();

    auto result = compactor.compactIfNeeded(history);
    REQUIRE(result.has_value());
    CHECK(*result == true);
    CHECK(history.estimatedTokenCount() < tokensBefore);
    CHECK(history.estimatedTokenCount() > 0);
}
