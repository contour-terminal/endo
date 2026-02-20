// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "History.hpp"

TEST_CASE("InMemoryHistory.basic_add_and_search", "[history]")
{
    auto history = endo::InMemoryHistory {};

    history.add("git status");
    history.add("git log");
    history.add("cmake --build");

    REQUIRE(history.size() == 3);

    auto const results = history.search("git", 10);
    REQUIRE(results.size() == 2);
    CHECK(results[0] == "git log"); // newest first
    CHECK(results[1] == "git status");
}

TEST_CASE("InMemoryHistory.trim_whitespace", "[history]")
{
    auto history = endo::InMemoryHistory {};

    history.add("  git status  ");
    REQUIRE(history.size() == 1);
    CHECK(history.entries().back() == "git status");

    history.add("\t cmake --build \n");
    REQUIRE(history.size() == 2);
    CHECK(history.entries().back() == "cmake --build");
}

TEST_CASE("InMemoryHistory.whitespace_only_not_added", "[history]")
{
    auto history = endo::InMemoryHistory {};

    history.add("   ");
    history.add("\t\n\r");
    history.add("");

    CHECK(history.size() == 0);
}

TEST_CASE("InMemoryHistory.trimmed_duplicates_detected", "[history]")
{
    auto history = endo::InMemoryHistory {};

    history.add("git status");
    history.add("  git status  "); // duplicate after trimming

    REQUIRE(history.size() == 1);
    CHECK(history.entries().back() == "git status");
}

TEST_CASE("InMemoryHistory.deduplication", "[history]")
{
    auto history = endo::InMemoryHistory {};

    history.add("git status");
    history.add("git log");
    history.add("git status"); // not consecutive duplicate ("git log" is most recent)

    REQUIRE(history.size() == 3);

    // Consecutive duplicate is suppressed
    history.add("git status");
    REQUIRE(history.size() == 3);
}
