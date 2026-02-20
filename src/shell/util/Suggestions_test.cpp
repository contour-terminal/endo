// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <ranges>
#include <string_view>

#include "Suggestions.hpp"

using namespace std::string_view_literals;

// =================================================================================================
// Suggestions tests
// =================================================================================================

TEST_CASE("Suggestions.levenshteinDistance")
{
    using endo::SuggestionGenerator;

    // Identical strings
    CHECK(SuggestionGenerator::levenshteinDistance("hello", "hello") == 0);

    // Single character difference
    CHECK(SuggestionGenerator::levenshteinDistance("hello", "hallo") == 1);

    // Insertion
    CHECK(SuggestionGenerator::levenshteinDistance("hello", "helloo") == 1);

    // Deletion
    CHECK(SuggestionGenerator::levenshteinDistance("hello", "helo") == 1);

    // Multiple differences
    CHECK(SuggestionGenerator::levenshteinDistance("kitten", "sitting") == 3);

    // Empty strings
    CHECK(SuggestionGenerator::levenshteinDistance("", "") == 0);
    CHECK(SuggestionGenerator::levenshteinDistance("abc", "") == 3);
    CHECK(SuggestionGenerator::levenshteinDistance("", "abc") == 3);
}

TEST_CASE("Suggestions.suggestCommand")
{
    using endo::SuggestionGenerator;

    std::array<std::string_view, 7> builtins = { "exit", "export", "cd", "read", "set", "true", "false" };

    // Typo in "exit"
    auto suggestions = SuggestionGenerator::suggestCommand("exti", builtins);
    REQUIRE(!suggestions.empty());
    CHECK(suggestions[0] == "exit");

    // Typo in "export"
    suggestions = SuggestionGenerator::suggestCommand("exoprt", builtins);
    REQUIRE(!suggestions.empty());
    CHECK(suggestions[0] == "export");

    // No close matches
    suggestions = SuggestionGenerator::suggestCommand("zzzzz", builtins);
    CHECK(suggestions.empty());

    // Exact match itself is excluded (but other close matches may appear)
    suggestions = SuggestionGenerator::suggestCommand("exit", builtins);
    // "exit" itself should not appear in suggestions
    CHECK(std::ranges::find(suggestions, "exit") == suggestions.end());
}

TEST_CASE("Suggestions.formatDidYouMean")
{
    using endo::SuggestionGenerator;

    CHECK(SuggestionGenerator::formatDidYouMean("exit") == "Did you mean 'exit'?");
    CHECK(SuggestionGenerator::formatDidYouMean("export") == "Did you mean 'export'?");
}
