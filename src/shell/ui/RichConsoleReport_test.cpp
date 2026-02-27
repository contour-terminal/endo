// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

#include "RichConsoleReport.hpp"

using namespace endo;
using CoreVM::FilePos;
using CoreVM::SourceLocation;
using CoreVM::diagnostics::Message;
using CoreVM::diagnostics::Type;

namespace
{

/// Creates a SourceLocation with the given parameters.
SourceLocation makeLoc(std::string filename, unsigned line, unsigned col, unsigned endCol)
{
    return SourceLocation(std::move(filename), FilePos { line, col, 0 }, FilePos { line, endCol, 0 });
}

} // namespace

// =================================================================================================
// formatDiagnostic plain text tests
// =================================================================================================

TEST_CASE("RichConsoleReport.plain_syntax_error", "[RichConsoleReport]")
{
    auto msg = Message(
        Type::SyntaxError, makeLoc("stdin", 1, 5, 18), "Unexpected token 'xyz'", {}, "ech \"hello world\"");

    auto const result = formatDiagnostic(msg, false);

    CHECK(result.find("error[syntax]: Unexpected token 'xyz'") != std::string::npos);
    CHECK(result.find("--> stdin:1:5") != std::string::npos);
    CHECK(result.find("ech \"hello world\"") != std::string::npos);
    CHECK(result.find("~") != std::string::npos);
}

TEST_CASE("RichConsoleReport.plain_warning", "[RichConsoleReport]")
{
    auto msg = Message(Type::Warning, makeLoc("test.sh", 3, 1, 5), "Unused variable 'x'");

    auto const result = formatDiagnostic(msg, false);

    CHECK(result.find("warning: Unused variable 'x'") != std::string::npos);
    CHECK(result.find("--> test.sh:3:1") != std::string::npos);
    // No error[...] bracket for warnings
    CHECK(result.find("error") == std::string::npos);
}

TEST_CASE("RichConsoleReport.plain_with_suggestions", "[RichConsoleReport]")
{
    auto msg = Message(Type::SyntaxError,
                       makeLoc("stdin", 1, 1, 4),
                       "Unknown command 'ech'",
                       { "Did you mean 'echo'?", "Did you mean 'each'?" },
                       "ech \"hello\"");

    auto const result = formatDiagnostic(msg, false);

    CHECK(result.find("hint: Did you mean 'echo'?") != std::string::npos);
    CHECK(result.find("hint: Did you mean 'each'?") != std::string::npos);
}

TEST_CASE("RichConsoleReport.plain_no_context", "[RichConsoleReport]")
{
    auto msg = Message(Type::LinkError, makeLoc("module.sh", 10, 1, 5), "Unresolved symbol 'foo'");

    auto const result = formatDiagnostic(msg, false);

    CHECK(result.find("error[link]: Unresolved symbol 'foo'") != std::string::npos);
    // No gutter lines when no context snippet
    CHECK(result.find("|") == std::string::npos);
}

TEST_CASE("RichConsoleReport.plain_no_filename", "[RichConsoleReport]")
{
    auto msg = Message(Type::TypeError, SourceLocation(), "Type mismatch");

    auto const result = formatDiagnostic(msg, false);

    CHECK(result.find("error[type]: Type mismatch") != std::string::npos);
    // No location line when filename is empty
    CHECK(result.find("-->") == std::string::npos);
}

// =================================================================================================
// formatDiagnostic colored output tests
// =================================================================================================

TEST_CASE("RichConsoleReport.colored_has_ansi_escapes", "[RichConsoleReport]")
{
    auto msg =
        Message(Type::SyntaxError, makeLoc("stdin", 1, 5, 10), "Unexpected token", {}, "ech \"hello\"");

    auto const result = formatDiagnostic(msg, true);

    // Should contain ANSI escape sequences
    CHECK(result.find("\033[") != std::string::npos);
    // Should contain bold
    CHECK(result.find("\033[1m") != std::string::npos);
    // Should contain reset
    CHECK(result.find("\033[m") != std::string::npos);
}

TEST_CASE("RichConsoleReport.colored_curly_underline", "[RichConsoleReport]")
{
    auto msg =
        Message(Type::SyntaxError, makeLoc("stdin", 1, 5, 10), "Unexpected token", {}, "ech \"hello\"");

    auto const result = formatDiagnostic(msg, true);

    // Should contain curly underline SGR sequence (4:3)
    CHECK(result.find("4:3m") != std::string::npos);
}

TEST_CASE("RichConsoleReport.colored_source_highlighting", "[RichConsoleReport]")
{
    auto msg = Message(Type::SyntaxError, makeLoc("stdin", 1, 5, 10), "Unexpected token", {}, "let x = 42");

    auto const result = formatDiagnostic(msg, true);

    // Should contain RGB foreground color sequences for syntax highlighting
    CHECK(result.find("\033[38;2;") != std::string::npos);
    // Individual characters from source should be present (interspersed with ANSI codes)
    CHECK(result.find('l') != std::string::npos);
    CHECK(result.find('4') != std::string::npos);
}

// =================================================================================================
// RichConsoleReport class tests
// =================================================================================================

TEST_CASE("RichConsoleReport.containsFailures_initially_false", "[RichConsoleReport]")
{
    auto report = RichConsoleReport();
    CHECK_FALSE(report.containsFailures());
}

TEST_CASE("RichConsoleReport.containsFailures_after_error", "[RichConsoleReport]")
{
    auto report = RichConsoleReport();
    report.push_back(Message(Type::SyntaxError, SourceLocation(), "test error"));
    CHECK(report.containsFailures());
}

TEST_CASE("RichConsoleReport.warning_does_not_count_as_failure", "[RichConsoleReport]")
{
    auto report = RichConsoleReport();
    report.push_back(Message(Type::Warning, SourceLocation(), "test warning"));
    CHECK_FALSE(report.containsFailures());
}

TEST_CASE("RichConsoleReport.token_error_label", "[RichConsoleReport]")
{
    auto msg = Message(Type::TokenError, makeLoc("test.sh", 1, 1, 2), "Invalid character");
    auto const result = formatDiagnostic(msg, false);
    CHECK(result.find("error[token]:") != std::string::npos);
}

TEST_CASE("RichConsoleReport.type_error_label", "[RichConsoleReport]")
{
    auto msg = Message(Type::TypeError, makeLoc("test.sh", 1, 1, 2), "Mismatched types");
    auto const result = formatDiagnostic(msg, false);
    CHECK(result.find("error[type]:") != std::string::npos);
}

TEST_CASE("RichConsoleReport.caret_position_matches_column", "[RichConsoleReport]")
{
    // Column 5 (1-based) → 4 spaces before the caret
    auto msg = Message(Type::SyntaxError, makeLoc("stdin", 1, 5, 6), "Error here", {}, "abcdefgh");

    auto const result = formatDiagnostic(msg, false);

    // The underline line should have 4 spaces then ~
    // Format: "  | " + "    ~"
    CHECK(result.find("|     ~") != std::string::npos);
}

TEST_CASE("RichConsoleReport.multi_char_underline", "[RichConsoleReport]")
{
    // Columns 3-7 (1-based) → 5 chars underlined
    auto msg = Message(Type::SyntaxError, makeLoc("stdin", 1, 3, 8), "Error span", {}, "abcdefgh");

    auto const result = formatDiagnostic(msg, false);

    CHECK(result.find("~~~~~") != std::string::npos);
}

// =================================================================================================
// BufferingConsoleReport tests
// =================================================================================================

TEST_CASE("BufferingConsoleReport.buffers_messages", "[BufferingConsoleReport]")
{
    auto report = BufferingConsoleReport();
    report.push_back(Message(Type::TypeError, SourceLocation(), "test error"));
    CHECK(report.containsFailures());
    CHECK(report.hasMessages());
    CHECK(report.formattedMessages().size() == 1);
    CHECK(report.formattedMessages()[0].find("test error") != std::string::npos);
}

TEST_CASE("BufferingConsoleReport.no_failures_when_empty", "[BufferingConsoleReport]")
{
    auto report = BufferingConsoleReport();
    CHECK_FALSE(report.containsFailures());
    CHECK_FALSE(report.hasMessages());
    CHECK(report.formattedMessages().empty());
}

TEST_CASE("BufferingConsoleReport.warnings_dont_count_as_failures", "[BufferingConsoleReport]")
{
    auto report = BufferingConsoleReport();
    report.push_back(Message(Type::Warning, SourceLocation(), "just a warning"));
    CHECK_FALSE(report.containsFailures());
    CHECK(report.hasMessages());
    CHECK(report.formattedMessages().size() == 1);
}

TEST_CASE("BufferingConsoleReport.multiple_messages", "[BufferingConsoleReport]")
{
    auto report = BufferingConsoleReport();
    report.push_back(Message(Type::TypeError, SourceLocation(), "error one"));
    report.push_back(Message(Type::SyntaxError, SourceLocation(), "error two"));
    CHECK(report.containsFailures());
    CHECK(report.formattedMessages().size() == 2);
}

TEST_CASE("BufferingConsoleReport.fills_missing_context_snippet", "[BufferingConsoleReport]")
{
    auto report = BufferingConsoleReport(ColorMode::Disabled);
    report.setSourceText("let x = 42");
    report.push_back(Message(Type::SyntaxError, makeLoc("stdin", 1, 5, 6), "Error here"));
    CHECK(report.hasMessages());
    // The formatted message should contain the source text
    CHECK(report.formattedMessages()[0].find("let x = 42") != std::string::npos);
}
