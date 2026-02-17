// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <agent/ConversationHistory.hpp>

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
