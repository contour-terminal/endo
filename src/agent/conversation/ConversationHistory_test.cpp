// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <agent/conversation/ConversationHistory.hpp>
#include <agent/conversation/TokenEstimator.hpp>

using namespace endo::agent;

TEST_CASE("ConversationHistory.empty_on_construction", "[agent]")
{
    auto history = ConversationHistory {};
    CHECK(history.empty());
    CHECK(history.size() == 0);
    CHECK(history.messages().empty());
}

TEST_CASE("ConversationHistory.add_and_query_messages", "[agent]")
{
    auto history = ConversationHistory {};
    history.addMessage(ChatMessage::text(Role::User, "Hello"));
    history.addMessage(ChatMessage::text(Role::Assistant, "Hi there!"));

    CHECK(history.size() == 2);
    CHECK_FALSE(history.empty());
    CHECK(history.messages()[0].role == Role::User);
    CHECK(history.messages()[0].textContent() == "Hello");
    CHECK(history.messages()[1].role == Role::Assistant);
    CHECK(history.messages()[1].textContent() == "Hi there!");
}

TEST_CASE("ConversationHistory.clear", "[agent]")
{
    auto history = ConversationHistory {};
    history.addMessage(ChatMessage::text(Role::User, "Hello"));
    history.addMessage(ChatMessage::text(Role::Assistant, "Hi"));

    history.clear();
    CHECK(history.empty());
    CHECK(history.size() == 0);
}

TEST_CASE("ConversationHistory.setSystemPrompt_inserts_when_none", "[agent]")
{
    auto history = ConversationHistory {};
    history.addMessage(ChatMessage::text(Role::User, "Hello"));

    history.setSystemPrompt("You are a helpful assistant.");

    CHECK(history.size() == 2);
    CHECK(history.messages()[0].role == Role::System);
    CHECK(history.messages()[0].textContent() == "You are a helpful assistant.");
    CHECK(history.messages()[1].role == Role::User);
    CHECK(history.messages()[1].textContent() == "Hello");
}

TEST_CASE("ConversationHistory.setSystemPrompt_replaces_existing", "[agent]")
{
    auto history = ConversationHistory {};
    history.setSystemPrompt("Old prompt");
    history.addMessage(ChatMessage::text(Role::User, "Hello"));

    history.setSystemPrompt("New prompt");

    CHECK(history.size() == 2);
    CHECK(history.messages()[0].role == Role::System);
    CHECK(history.messages()[0].textContent() == "New prompt");
    CHECK(history.messages()[1].role == Role::User);
}

TEST_CASE("ConversationHistory.setSystemPrompt_on_empty", "[agent]")
{
    auto history = ConversationHistory {};
    history.setSystemPrompt("System prompt");

    CHECK(history.size() == 1);
    CHECK(history.messages()[0].role == Role::System);
    CHECK(history.messages()[0].textContent() == "System prompt");
}

// ============================================================================
// Token tracking tests
// ============================================================================

TEST_CASE("ConversationHistory.token_count_starts_at_zero", "[agent]")
{
    auto history = ConversationHistory {};
    CHECK(history.estimatedTokenCount() == 0);
}

TEST_CASE("ConversationHistory.token_count_grows_on_addMessage", "[agent]")
{
    auto history = ConversationHistory {};
    history.addMessage(ChatMessage::text(Role::User, "Hello world"));

    auto const expected = estimateTokenCount(ChatMessage::text(Role::User, "Hello world"));
    CHECK(history.estimatedTokenCount() == expected);
    CHECK(history.estimatedTokenCount() > 0);
}

TEST_CASE("ConversationHistory.token_count_accumulates", "[agent]")
{
    auto history = ConversationHistory {};
    history.addMessage(ChatMessage::text(Role::User, "Hello"));
    auto const afterFirst = history.estimatedTokenCount();

    history.addMessage(ChatMessage::text(Role::Assistant, "Hi there, how can I help you today?"));
    CHECK(history.estimatedTokenCount() > afterFirst);
}

TEST_CASE("ConversationHistory.clear_resets_token_count", "[agent]")
{
    auto history = ConversationHistory {};
    history.addMessage(ChatMessage::text(Role::User, "Hello"));
    history.addMessage(ChatMessage::text(Role::Assistant, "Hi"));
    CHECK(history.estimatedTokenCount() > 0);

    history.clear();
    CHECK(history.estimatedTokenCount() == 0);
}

TEST_CASE("ConversationHistory.setSystemPrompt_adjusts_token_count", "[agent]")
{
    auto history = ConversationHistory {};
    history.setSystemPrompt("Short prompt");
    auto const shortTokens = history.estimatedTokenCount();

    history.setSystemPrompt("This is a much longer system prompt with many more tokens in it");
    auto const longTokens = history.estimatedTokenCount();
    CHECK(longTokens > shortTokens);
}

TEST_CASE("ConversationHistory.setSystemPrompt_insert_adds_tokens", "[agent]")
{
    auto history = ConversationHistory {};
    history.addMessage(ChatMessage::text(Role::User, "Hello"));
    auto const beforePrompt = history.estimatedTokenCount();

    history.setSystemPrompt("System prompt");
    CHECK(history.estimatedTokenCount() > beforePrompt);
}

TEST_CASE("ConversationHistory.replaceMessages_recalculates", "[agent]")
{
    auto history = ConversationHistory {};
    history.addMessage(ChatMessage::text(Role::User, "Hello"));
    history.addMessage(ChatMessage::text(Role::Assistant, "Response with lots of text to make it longer"));
    auto const beforeReplace = history.estimatedTokenCount();

    // Replace with fewer messages
    auto newMessages = std::vector<ChatMessage> {};
    newMessages.push_back(ChatMessage::text(Role::System, "Summary"));
    history.replaceMessages(std::move(newMessages));

    CHECK(history.size() == 1);
    CHECK(history.estimatedTokenCount() < beforeReplace);
    CHECK(history.estimatedTokenCount() > 0);
    CHECK(history.messages()[0].textContent() == "Summary");
}

TEST_CASE("ConversationHistory.replaceMessages_with_empty", "[agent]")
{
    auto history = ConversationHistory {};
    history.addMessage(ChatMessage::text(Role::User, "Hello"));

    history.replaceMessages({});
    CHECK(history.empty());
    CHECK(history.estimatedTokenCount() == 0);
}
