// SPDX-License-Identifier: Apache-2.0

#include <shell/DiagnosticsAdapter.h>
#include <shell/Suggestions.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <ranges>
#include <string_view>

import CoreVM;
import Lexer;

using namespace std::string_view_literals;

// =================================================================================================
// DiagnosticsAdapter tests
// =================================================================================================

TEST_CASE("DiagnosticsAdapter.toCoreLoc_SourceLocationRange")
{
    endo::SourceLocationRange range { .begin = { .line = 0, .column = 5 },
                                      .end = { .line = 0, .column = 10 },
                                      .name = "test.sh" };

    auto const loc = endo::toCoreLoc(range);

    // CoreVM uses 1-based line/column
    CHECK(loc.filename == "test.sh");
    CHECK(loc.begin.line == 1);
    CHECK(loc.begin.column == 6);
    CHECK(loc.end.line == 1);
    CHECK(loc.end.column == 11);
}

TEST_CASE("DiagnosticsAdapter.toCoreLoc_SourceLocation")
{
    endo::SourceLocation loc { .line = 2, .column = 7, .name = "script.sh" };

    auto const coreLoc = endo::toCoreLoc(loc);

    CHECK(coreLoc.filename == "script.sh");
    CHECK(coreLoc.begin.line == 3);
    CHECK(coreLoc.begin.column == 8);
}

TEST_CASE("DiagnosticsAdapter.extractSourceLine")
{
    auto const source = "line one\nline two\nline three\n"sv;

    CHECK(endo::extractSourceLine(source, 0) == "line one");
    CHECK(endo::extractSourceLine(source, 1) == "line two");
    CHECK(endo::extractSourceLine(source, 2) == "line three");
    CHECK(endo::extractSourceLine(source, 3) == "");
    CHECK(endo::extractSourceLine(source, -1) == "");
}

TEST_CASE("DiagnosticsAdapter.extractSourceLine_NoTrailingNewline")
{
    auto const source = "first line\nsecond line"sv;

    CHECK(endo::extractSourceLine(source, 0) == "first line");
    CHECK(endo::extractSourceLine(source, 1) == "second line");
}

TEST_CASE("DiagnosticsAdapter.createCaretLine")
{
    CHECK(endo::createCaretLine(0, 1) == "^");
    CHECK(endo::createCaretLine(5, 1) == "     ^");
    CHECK(endo::createCaretLine(3, 4) == "   ^~~~");
    CHECK(endo::createCaretLine(0, 10) == "^~~~~~~~~~");
}

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

// =================================================================================================
// Integration tests for error reporting
// =================================================================================================

TEST_CASE("Diagnostics.Message_with_suggestions")
{
    CoreVM::diagnostics::Message msg(
        CoreVM::diagnostics::Type::SyntaxError,
        CoreVM::SourceLocation("test.sh", CoreVM::FilePos { 1, 5 }, CoreVM::FilePos { 1, 9 }),
        "Unknown command 'exti'",
        { "Did you mean 'exit'?" },
        "exti arg1 arg2");

    CHECK(msg.type == CoreVM::diagnostics::Type::SyntaxError);
    CHECK(msg.text == "Unknown command 'exti'");
    CHECK(msg.suggestions.size() == 1);
    CHECK(msg.suggestions[0] == "Did you mean 'exit'?");
    CHECK(msg.contextSnippet.has_value());
    CHECK(msg.contextSnippet.value() == "exti arg1 arg2");
}

TEST_CASE("Diagnostics.Message_backward_compatible")
{
    // Old-style construction should still work
    CoreVM::diagnostics::Message msg(
        CoreVM::diagnostics::Type::TypeError, CoreVM::SourceLocation("test.sh"), "Type mismatch");

    CHECK(msg.type == CoreVM::diagnostics::Type::TypeError);
    CHECK(msg.text == "Type mismatch");
    CHECK(msg.suggestions.empty());
    CHECK(!msg.contextSnippet.has_value());
}
