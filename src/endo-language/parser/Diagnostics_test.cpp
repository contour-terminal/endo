// SPDX-License-Identifier: Apache-2.0

#include <endo-language/lexer/Lexer.hpp>
#include <endo-language/parser/DiagnosticsAdapter.hpp>

#include <CoreVM/CoreVM.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string_view>

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
    CHECK(endo::extractSourceLine(source, 3).empty());
    CHECK(endo::extractSourceLine(source, -1).empty());
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
