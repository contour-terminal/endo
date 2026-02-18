// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <agent/AgentHistoryProvider.hpp>

using namespace endo::agent;

TEST_CASE("AgentHistoryProvider.empty_history_no_completions")
{
    auto provider = AgentHistoryProvider {};
    auto items = provider.complete("hello", 5);
    CHECK(items.empty());
}

TEST_CASE("AgentHistoryProvider.prefix_matching")
{
    auto provider = AgentHistoryProvider {};
    provider.addEntry("refactor the login flow");
    provider.addEntry("review the test coverage");
    provider.addEntry("add unit tests");

    auto items = provider.complete("ref", 3);
    REQUIRE(items.size() == 1);
    CHECK(items[0].text == "refactor the login flow");
}

TEST_CASE("AgentHistoryProvider.fuzzy_matching")
{
    auto provider = AgentHistoryProvider {};
    provider.addEntry("refactor the login flow");
    provider.addEntry("add unit tests");

    // "rfl" should fuzzy-match "refactor the login flow" (r..f..l..)
    auto items = provider.complete("rfl", 3);
    REQUIRE(items.size() == 1);
    CHECK(items[0].text == "refactor the login flow");
}

TEST_CASE("AgentHistoryProvider.recency_ordering")
{
    auto provider = AgentHistoryProvider {};
    provider.addEntry("fix old bug");
    provider.addEntry("fix new bug");

    auto items = provider.complete("fix", 3);
    REQUIRE(items.size() == 2);
    // Most recent ("fix new bug") should have higher score.
    CHECK(items[0].text == "fix new bug");
    CHECK(items[1].text == "fix old bug");
}

TEST_CASE("AgentHistoryProvider.deduplication")
{
    auto provider = AgentHistoryProvider {};
    provider.addEntry("hello world");
    provider.addEntry("foo bar");
    provider.addEntry("hello world"); // Duplicate — should be kept only once.

    CHECK(provider.entries().size() == 2);

    auto items = provider.complete("hello", 5);
    REQUIRE(items.size() == 1);
    CHECK(items[0].text == "hello world");
}

TEST_CASE("AgentHistoryProvider.does_not_suggest_exact_input")
{
    auto provider = AgentHistoryProvider {};
    provider.addEntry("hello world");

    // Exact match of full text should not be suggested.
    auto items = provider.complete("hello world", 11);
    CHECK(items.empty());
}

TEST_CASE("AgentHistoryProvider.slash_commands_ignored")
{
    auto provider = AgentHistoryProvider {};
    provider.addEntry("/help");
    provider.addEntry("normal query");

    // Slash command input should not produce history completions.
    auto items = provider.complete("/he", 3);
    CHECK(items.empty());
}

TEST_CASE("AgentHistoryProvider.priority_value")
{
    auto provider = AgentHistoryProvider {};
    CHECK(provider.priority() == 50);
}

TEST_CASE("AgentHistoryProvider.empty_input_no_completions")
{
    auto provider = AgentHistoryProvider {};
    provider.addEntry("hello world");

    auto items = provider.complete("", 0);
    CHECK(items.empty());
}

TEST_CASE("AgentHistoryProvider.bulk_set_entries")
{
    auto provider = AgentHistoryProvider {};
    provider.setEntries({ "alpha", "beta", "gamma" });

    CHECK(provider.entries().size() == 3);

    auto items = provider.complete("al", 2);
    REQUIRE(items.size() == 1);
    CHECK(items[0].text == "alpha");
}

TEST_CASE("AgentHistoryProvider.case_insensitive_prefix")
{
    auto provider = AgentHistoryProvider {};
    provider.addEntry("Refactor the code");

    // Lowercase query should match uppercase entry (smart case).
    auto items = provider.complete("refactor", 8);
    REQUIRE(items.size() == 1);
    CHECK(items[0].text == "Refactor the code");
}
