// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "Lexer.hpp"

TEST_CASE("Lexer.basic")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("echo hello world $PATH"));
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "echo");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "hello");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "world");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DollarName);
    CHECK(lexer.currentLiteral() == "PATH");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::EndOfInput);
}

TEST_CASE("Lexer.utf8_identifier")
{
    // Chinese characters: 中文
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("echo \xE4\xB8\xAD\xE6\x96\x87"));
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "echo");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "\xE4\xB8\xAD\xE6\x96\x87"); // Chinese characters
}

TEST_CASE("Lexer.utf8_string")
{
    // Chinese characters in quoted string: "中文"
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("echo \"\xE4\xB8\xAD\xE6\x96\x87\""));
    CHECK(lexer.currentToken() == endo::Token::Identifier);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DblQuoteStart);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::StringFragment);
    CHECK(lexer.currentLiteral() == "\xE4\xB8\xAD\xE6\x96\x87");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DblQuoteEnd);
}

TEST_CASE("Lexer.utf8_emoji")
{
    // Grinning face emoji: U+1F600
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("echo \xF0\x9F\x98\x80"));
    CHECK(lexer.currentToken() == endo::Token::Identifier);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "\xF0\x9F\x98\x80"); // Grinning face emoji
}

TEST_CASE("Lexer.utf8_mixed")
{
    // Mixed ASCII and UTF-8: hello世界
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("hello\xE4\xB8\x96\xE7\x95\x8C"));
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "hello\xE4\xB8\x96\xE7\x95\x8C");
}

TEST_CASE("Lexer.logical_and")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("cmd1 && cmd2"));
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "cmd1");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::AmpAmp);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "cmd2");
}

TEST_CASE("Lexer.logical_or")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("cmd1 || cmd2"));
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "cmd1");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::PipePipe);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "cmd2");
}

TEST_CASE("Lexer.pipe_vs_logical_or")
{
    // Ensure | and || are distinguished correctly
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("a | b || c"));
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "a");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Pipe);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "b");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::PipePipe);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "c");
}

// ============================================================================
// Command Substitution Tokens
// ============================================================================

TEST_CASE("Lexer.dollar_rnd_open")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("$(echo hello)"));
    CHECK(lexer.currentToken() == endo::Token::DollarRndOpen);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "echo");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "hello");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::RndClose);
}

TEST_CASE("Lexer.backtick")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("`echo hello`"));
    CHECK(lexer.currentToken() == endo::Token::Backtick);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "echo");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "hello");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Backtick);
}

TEST_CASE("Lexer.greater_rnd_open")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>(">(cat)"));
    CHECK(lexer.currentToken() == endo::Token::GreaterRndOpen);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "cat");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::RndClose);
}

TEST_CASE("Lexer.less_rnd_open")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("<(echo test)"));
    CHECK(lexer.currentToken() == endo::Token::LessRndOpen);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "echo");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "test");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::RndClose);
}

TEST_CASE("Lexer.dollar_vs_dollar_rnd_open")
{
    // Ensure $ followed by variable is distinguished from $(
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("$VAR $(cmd)"));
    CHECK(lexer.currentToken() == endo::Token::DollarName);
    CHECK(lexer.currentLiteral() == "VAR");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DollarRndOpen);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "cmd");
}

TEST_CASE("Lexer.greater_vs_greater_rnd_open")
{
    // Ensure > is distinguished from >(
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("> file >(cmd)"));
    CHECK(lexer.currentToken() == endo::Token::Greater);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "file");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::GreaterRndOpen);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "cmd");
}

TEST_CASE("Lexer.unterminated_double_quote_string")
{
    // Unterminated double-quoted string should return Invalid token (not hang)
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("echo \"hello"));
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "echo");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DblQuoteStart);

    lexer.nextToken();
    // Should get the fragment content, then Invalid on next call
    CHECK(lexer.currentToken() == endo::Token::StringFragment);
    CHECK(lexer.currentLiteral() == "hello");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Invalid);
}

TEST_CASE("Lexer.unterminated_single_quote_string")
{
    // Unterminated single-quoted string should return Invalid token (not hang)
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("echo 'hello"));
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "echo");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Invalid);
}

TEST_CASE("Lexer.unterminated_string_with_escape")
{
    // Unterminated string with trailing escape should return Invalid token (not hang)
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("echo \"hello\\"));
    CHECK(lexer.currentToken() == endo::Token::Identifier);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DblQuoteStart);

    lexer.nextToken();
    // The backslash at EOF causes Invalid
    CHECK(lexer.currentToken() == endo::Token::Invalid);
}

// ============================================================================
// String Interpolation Tests
// ============================================================================

TEST_CASE("Lexer.dquote_simple_string")
{
    // Simple double-quoted string without interpolation
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("\"hello world\""));
    CHECK(lexer.currentToken() == endo::Token::DblQuoteStart);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::StringFragment);
    CHECK(lexer.currentLiteral() == "hello world");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DblQuoteEnd);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::EndOfInput);
}

TEST_CASE("Lexer.dquote_empty_string")
{
    // Empty double-quoted string
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("\"\""));
    CHECK(lexer.currentToken() == endo::Token::DblQuoteStart);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DblQuoteEnd);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::EndOfInput);
}

TEST_CASE("Lexer.dquote_variable_only")
{
    // Double-quoted string with only a variable
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("\"$USER\""));
    CHECK(lexer.currentToken() == endo::Token::DblQuoteStart);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DollarName);
    CHECK(lexer.currentLiteral() == "USER");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DblQuoteEnd);
}

TEST_CASE("Lexer.dquote_braced_variable")
{
    // Double-quoted string with braced variable
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("\"${USER}\""));
    CHECK(lexer.currentToken() == endo::Token::DblQuoteStart);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DollarBraceName);
    CHECK(lexer.currentLiteral() == "USER");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DblQuoteEnd);
}

TEST_CASE("Lexer.dquote_variable_with_text")
{
    // Double-quoted string with text before and after variable
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("\"hello $USER!\""));
    CHECK(lexer.currentToken() == endo::Token::DblQuoteStart);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::StringFragment);
    CHECK(lexer.currentLiteral() == "hello ");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DollarName);
    CHECK(lexer.currentLiteral() == "USER");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::StringFragment);
    CHECK(lexer.currentLiteral() == "!");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DblQuoteEnd);
}

TEST_CASE("Lexer.dquote_multiple_variables")
{
    // Multiple variables in one string
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("\"$FIRST$SECOND\""));
    CHECK(lexer.currentToken() == endo::Token::DblQuoteStart);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DollarName);
    CHECK(lexer.currentLiteral() == "FIRST");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DollarName);
    CHECK(lexer.currentLiteral() == "SECOND");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DblQuoteEnd);
}

TEST_CASE("Lexer.dquote_command_substitution")
{
    // Command substitution inside double quotes
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("\"Today is $(date)\""));
    CHECK(lexer.currentToken() == endo::Token::DblQuoteStart);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::StringFragment);
    CHECK(lexer.currentLiteral() == "Today is ");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DollarRndOpen);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "date");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::RndClose);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DblQuoteEnd);
}

TEST_CASE("Lexer.dquote_backtick_substitution")
{
    // Backtick command substitution inside double quotes
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("\"Hello `whoami`\""));
    CHECK(lexer.currentToken() == endo::Token::DblQuoteStart);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::StringFragment);
    CHECK(lexer.currentLiteral() == "Hello ");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Backtick);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "whoami");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Backtick);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DblQuoteEnd);
}

TEST_CASE("Lexer.dquote_arithmetic_expansion")
{
    // Arithmetic expansion inside double quotes
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("\"Sum: $((1+2))\""));
    CHECK(lexer.currentToken() == endo::Token::DblQuoteStart);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::StringFragment);
    CHECK(lexer.currentLiteral() == "Sum: ");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DollarDblRndOpen);

    // After $((, we should get the arithmetic content tokens
    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Number);
    CHECK(lexer.currentLiteral() == "1");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "+");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Number);
    CHECK(lexer.currentLiteral() == "2");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DblRndClose);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DblQuoteEnd);
}

TEST_CASE("Lexer.dquote_variable_with_comma")
{
    // Variable followed by comma in double quotes
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("\"$USER, hello\""));
    CHECK(lexer.currentToken() == endo::Token::DblQuoteStart);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DollarName);
    CHECK(lexer.currentLiteral() == "USER");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::StringFragment);
    CHECK(lexer.currentLiteral() == ", hello");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DblQuoteEnd);
}

TEST_CASE("Lexer.dquote_special_variables")
{
    // Special variables inside double quotes: $?, $$, $!
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("\"Exit: $? PID: $$\""));
    CHECK(lexer.currentToken() == endo::Token::DblQuoteStart);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::StringFragment);
    CHECK(lexer.currentLiteral() == "Exit: ");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DollarQuestion);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::StringFragment);
    CHECK(lexer.currentLiteral() == " PID: ");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DollarDollar);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DblQuoteEnd);
}

TEST_CASE("Lexer.dquote_positional_param")
{
    // Positional parameter inside double quotes
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("\"First arg: $1\""));
    CHECK(lexer.currentToken() == endo::Token::DblQuoteStart);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::StringFragment);
    CHECK(lexer.currentLiteral() == "First arg: ");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DollarNumber);
    CHECK(lexer.currentLiteral() == "1");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DblQuoteEnd);
}

TEST_CASE("Lexer.dquote_escaped_dollar")
{
    // Escaped dollar sign should be literal
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("\"Price: \\$100\""));
    CHECK(lexer.currentToken() == endo::Token::DblQuoteStart);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::StringFragment);
    CHECK(lexer.currentLiteral() == "Price: $100");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DblQuoteEnd);
}

TEST_CASE("Lexer.dquote_escaped_quote")
{
    // Escaped double quote inside string
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("\"He said \\\"hi\\\"\""));
    CHECK(lexer.currentToken() == endo::Token::DblQuoteStart);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::StringFragment);
    CHECK(lexer.currentLiteral() == "He said \"hi\"");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DblQuoteEnd);
}

TEST_CASE("Lexer.dquote_escaped_backslash")
{
    // Escaped backslash
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("\"path\\\\name\""));
    CHECK(lexer.currentToken() == endo::Token::DblQuoteStart);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::StringFragment);
    CHECK(lexer.currentLiteral() == "path\\name");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DblQuoteEnd);
}

TEST_CASE("Lexer.dquote_escape_sequences")
{
    // Common escape sequences: \n, \t, \r
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("\"line1\\nline2\\ttab\""));
    CHECK(lexer.currentToken() == endo::Token::DblQuoteStart);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::StringFragment);
    CHECK(lexer.currentLiteral() == "line1\nline2\ttab");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DblQuoteEnd);
}

TEST_CASE("Lexer.dquote_param_expansion")
{
    // Parameter expansion inside double quotes
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("\"${VAR:-default}\""));
    CHECK(lexer.currentToken() == endo::Token::DblQuoteStart);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DollarBraceParam);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DblQuoteEnd);
}

TEST_CASE("Lexer.single_quote_no_interpolation")
{
    // Single-quoted strings should NOT interpolate
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("'$USER'"));
    CHECK(lexer.currentToken() == endo::Token::String);
    CHECK(lexer.currentLiteral() == "$USER");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::EndOfInput);
}

TEST_CASE("Lexer.dquote_complex_example")
{
    // Complex example with multiple interpolation types
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("\"hello ${USER}, or $USER\""));
    CHECK(lexer.currentToken() == endo::Token::DblQuoteStart);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::StringFragment);
    CHECK(lexer.currentLiteral() == "hello ");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DollarBraceName);
    CHECK(lexer.currentLiteral() == "USER");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::StringFragment);
    CHECK(lexer.currentLiteral() == ", or ");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DollarName);
    CHECK(lexer.currentLiteral() == "USER");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DblQuoteEnd);
}

// ============================================================================
// F# Style Keyword Tests
// ============================================================================

TEST_CASE("Lexer.fsharp_let")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("let x = 42"));
    CHECK(lexer.currentToken() == endo::Token::Let);
    CHECK(lexer.currentLiteral() == "let");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "x");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Equal);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Number);
    CHECK(lexer.currentLiteral() == "42");
}

TEST_CASE("Lexer.fsharp_let_mut")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("let mut counter = 0"));
    CHECK(lexer.currentToken() == endo::Token::Let);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Mut);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "counter");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Equal);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Number);
    CHECK(lexer.currentLiteral() == "0");
}

TEST_CASE("Lexer.fsharp_fun_keyword")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("fun x -> x + 1"));
    CHECK(lexer.currentToken() == endo::Token::Fun);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "x");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Arrow);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "x");
}

TEST_CASE("Lexer.fsharp_match_with")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("match x with"));
    CHECK(lexer.currentToken() == endo::Token::Match);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "x");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::With);
}

TEST_CASE("Lexer.fsharp_when_guard")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("when x > 0"));
    CHECK(lexer.currentToken() == endo::Token::When);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "x");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Greater);
}

TEST_CASE("Lexer.fsharp_type_keyword")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("type Point = { x: int }"));
    CHECK(lexer.currentToken() == endo::Token::Type);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "Point");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Equal);
}

TEST_CASE("Lexer.fsharp_of_keyword")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("Some of int"));
    CHECK(lexer.currentToken() == endo::Token::OptionSome);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Of);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "int");
}

TEST_CASE("Lexer.fsharp_rec_and")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("let rec fib n = n and foo x = x"));
    CHECK(lexer.currentToken() == endo::Token::Let);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Rec);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "fib");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "n");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Equal);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "n");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::And);
}

TEST_CASE("Lexer.fsharp_as_pattern")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("x as y"));
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "x");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::As);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "y");
}

TEST_CASE("Lexer.fsharp_in_not_keyword")
{
    // 'in' is NOT tokenized as a keyword to preserve bash for-loops
    // The parser recognizes 'in' contextually for F# let-in expressions
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("let x = 1 in x + 1"));
    CHECK(lexer.currentToken() == endo::Token::Let);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "x");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Equal);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Number);
    CHECK(lexer.currentLiteral() == "1");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier); // 'in' is an identifier
    CHECK(lexer.currentLiteral() == "in");
}

// ============================================================================
// F# Style Constructor Tests
// ============================================================================

TEST_CASE("Lexer.fsharp_option_some")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("Some 42"));
    CHECK(lexer.currentToken() == endo::Token::OptionSome);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Number);
    CHECK(lexer.currentLiteral() == "42");
}

TEST_CASE("Lexer.fsharp_option_none")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("None"));
    CHECK(lexer.currentToken() == endo::Token::OptionNone);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::EndOfInput);
}

TEST_CASE("Lexer.fsharp_result_ok")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("Ok value"));
    CHECK(lexer.currentToken() == endo::Token::ResultOk);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "value");
}

// ============================================================================
// F# Style Operator Tests
// ============================================================================

TEST_CASE("Lexer.fsharp_arrow")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("x -> y"));
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "x");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Arrow);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "y");
}

TEST_CASE("Lexer.fsharp_left_arrow")
{
    // Mutation assignment: x <- value
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("x <- 42"));
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "x");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::LeftArrow);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Number);
    CHECK(lexer.currentLiteral() == "42");
}

TEST_CASE("Lexer.fsharp_forward_pipe")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("x |> f"));
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "x");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::ForwardPipe);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "f");
}

TEST_CASE("Lexer.fsharp_forward_pipe_vs_shell_pipe")
{
    // Ensure |> and | are distinguished correctly
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("a | b |> c"));
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "a");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Pipe);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "b");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::ForwardPipe);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "c");
}

TEST_CASE("Lexer.fsharp_double_colon_in_identifier")
{
    // :: is NOT tokenized separately to preserve shell compatibility
    // The parser handles :: for cons in F# contexts
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("1::rest"));
    CHECK(lexer.currentToken() == endo::Token::Number);
    CHECK(lexer.currentLiteral() == "1");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "::rest"); // :: is part of identifier
}

TEST_CASE("Lexer.fsharp_colon_in_value")
{
    // : is NOT tokenized separately to preserve shell compatibility (PATH, etc.)
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("/usr/bin:/usr/local/bin"));
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "/usr/bin:/usr/local/bin"); // entire PATH is one token
}

TEST_CASE("Lexer.fsharp_question_in_glob")
{
    // ? is NOT tokenized separately to preserve shell glob patterns
    // The parser handles ? for error propagation in F# contexts
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("file?.txt"));
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "file?.txt"); // ? is part of identifier
}

TEST_CASE("Lexer.fsharp_comma_in_brace")
{
    // , is NOT tokenized separately to preserve shell brace expansion
    // The parser handles , for tuples in F# contexts
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("{a,b,c}"));
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "{a,b,c}"); // entire brace expansion is one token
}

TEST_CASE("Lexer.fsharp_range_lexer_behavior")
{
    // Range syntax 1..10 is lexed as: Number(1), DotDot(..), Number(10)
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("1..10"));
    lexer.enterFSharpExpr();
    CHECK(lexer.currentToken() == endo::Token::Number);
    CHECK(lexer.currentLiteral() == "1");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::DotDot);
    CHECK(lexer.currentLiteral() == "");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Number);
    CHECK(lexer.currentLiteral() == "10");
}

TEST_CASE("Lexer.shell_mode_dotdot_as_identifier")
{
    // In shell mode, .. should be an identifier (e.g., cd ..)
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("cd .."));
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "cd");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "..");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::EndOfInput);
}

TEST_CASE("Lexer.shell_mode_dotdot_path")
{
    // In shell mode, ../foo should be an identifier (path traversal)
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("ls ../foo"));
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "ls");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "../foo");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::EndOfInput);
}

TEST_CASE("Lexer.ellipsis_always_produces_token")
{
    // ... always produces Ellipsis token (used for variadic splat in both modes)
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("..."));
    CHECK(lexer.currentToken() == endo::Token::Ellipsis);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::EndOfInput);
}

// ============================================================================
// F# Compatibility with Shell Syntax
// ============================================================================

TEST_CASE("Lexer.shell_flags_preserved")
{
    // Command-line flags should still work
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("ls -la --help"));
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "ls");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "-la");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "--help");
}

TEST_CASE("Lexer.shell_filenames_with_dashes")
{
    // Filenames with dashes should still work
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("cat my-file-name.txt"));
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "cat");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "my-file-name.txt");
}

TEST_CASE("Lexer.shell_glob_patterns_preserved")
{
    // Glob patterns should still work
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("ls [a-z]*.txt"));
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "ls");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "[a-z]*.txt");
}

TEST_CASE("Lexer.bracket_at_statement_start_is_list")
{
    // [ at statement start should produce BracketOpen (list literal), not shell identifier
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("[1; 2; 3]"));
    CHECK(lexer.currentToken() == endo::Token::BracketOpen);
}

TEST_CASE("Lexer.bracket_after_newline_is_list")
{
    // [ after newline (statement start) should produce BracketOpen
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("echo hello\n[1; 2]"));
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "echo");

    lexer.nextToken(); // hello
    lexer.nextToken(); // LineFeed
    CHECK(lexer.currentToken() == endo::Token::LineFeed);

    lexer.nextToken(); // [ at statement start
    CHECK(lexer.currentToken() == endo::Token::BracketOpen);
}

TEST_CASE("Lexer.shell_redirect_append")
{
    // >> should still work for redirect append
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("echo hello >> file.txt"));
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "echo");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "hello");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::GreaterGreater);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "file.txt");
}

TEST_CASE("Lexer.shell_here_doc")
{
    // << should still work for here-documents
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("cat << EOF"));
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "cat");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::LessLess);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "EOF");
}

TEST_CASE("Lexer.fsharp_complex_expression")
{
    // Complex F# style expression
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("let add x y = x + y |> Some"));
    CHECK(lexer.currentToken() == endo::Token::Let);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "add");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "x");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "y");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Equal);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "x");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "+");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "y");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::ForwardPipe);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::OptionSome);
}

TEST_CASE("Lexer.fsharp_match_arm")
{
    // Match arm with arrow
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("| Some x -> x"));
    CHECK(lexer.currentToken() == endo::Token::Pipe);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::OptionSome);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "x");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Arrow);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "x");
}

TEST_CASE("Lexer.fsharp_type_annotation_lexer_behavior")
{
    // At the lexer level, : is not tokenized separately
    // The parser will need to handle type annotations
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("let x: int = 42"));
    CHECK(lexer.currentToken() == endo::Token::Let);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "x:"); // x: is one token

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "int");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Equal);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Number);
    CHECK(lexer.currentLiteral() == "42");
}

// ============================================================================
// Numeric Base Literals
// ============================================================================

TEST_CASE("Lexer.hex_literal")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("0xFF"));
    CHECK(lexer.currentToken() == endo::Token::Number);
    CHECK(lexer.currentLiteral() == "0xFF");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::EndOfInput);
}

TEST_CASE("Lexer.hex_literal_uppercase")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("0XFF"));
    CHECK(lexer.currentToken() == endo::Token::Number);
    CHECK(lexer.currentLiteral() == "0XFF");
}

TEST_CASE("Lexer.octal_literal")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("0o755"));
    CHECK(lexer.currentToken() == endo::Token::Number);
    CHECK(lexer.currentLiteral() == "0o755");
}

TEST_CASE("Lexer.binary_literal")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("0b1010"));
    CHECK(lexer.currentToken() == endo::Token::Number);
    CHECK(lexer.currentLiteral() == "0b1010");
}

TEST_CASE("Lexer.negative_hex")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("-0xFF"));
    CHECK(lexer.currentToken() == endo::Token::Number);
    CHECK(lexer.currentLiteral() == "-0xFF");
}

TEST_CASE("Lexer.scientific_notation")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("1e10"));
    CHECK(lexer.currentToken() == endo::Token::Number);
    CHECK(lexer.currentLiteral() == "1e10");
}

TEST_CASE("Lexer.scientific_notation_with_decimal")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("2.5e-3"));
    CHECK(lexer.currentToken() == endo::Token::Number);
    CHECK(lexer.currentLiteral() == "2.5e-3");
}

TEST_CASE("Lexer.hex_zero")
{
    // Edge case: 0x0
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("0x0"));
    CHECK(lexer.currentToken() == endo::Token::Number);
    CHECK(lexer.currentLiteral() == "0x0");
}

TEST_CASE("Lexer.binary_single_bit")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("0b1"));
    CHECK(lexer.currentToken() == endo::Token::Number);
    CHECK(lexer.currentLiteral() == "0b1");
}

// ============================================================================
// Comment Tests
// ============================================================================

TEST_CASE("Lexer.comment_hash")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("42 # comment"));
    CHECK(lexer.currentToken() == endo::Token::Number);
    CHECK(lexer.currentLiteral() == "42");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::EndOfInput);
}

TEST_CASE("Lexer.comment_double_slash")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("42 // comment"));
    CHECK(lexer.currentToken() == endo::Token::Number);
    CHECK(lexer.currentLiteral() == "42");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::EndOfInput);
}

TEST_CASE("Lexer.comment_block")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("(* comment *) 42"));
    CHECK(lexer.currentToken() == endo::Token::Number);
    CHECK(lexer.currentLiteral() == "42");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::EndOfInput);
}

TEST_CASE("Lexer.comment_block_nested")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("(* outer (* inner *) *) 42"));
    CHECK(lexer.currentToken() == endo::Token::Number);
    CHECK(lexer.currentLiteral() == "42");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::EndOfInput);
}

TEST_CASE("Lexer.comment_hash_at_start")
{
    // # at the very start is a comment
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("# whole line comment\n42"));
    CHECK(lexer.currentToken() == endo::Token::LineFeed);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Number);
    CHECK(lexer.currentLiteral() == "42");
}

TEST_CASE("Lexer.comment_double_slash_multiline")
{
    // // comment only spans one line
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("// comment\n42"));
    CHECK(lexer.currentToken() == endo::Token::LineFeed);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Number);
    CHECK(lexer.currentLiteral() == "42");
}

TEST_CASE("Lexer.comment_block_inline")
{
    // Block comment between tokens
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("10 (* add *) 20"));
    CHECK(lexer.currentToken() == endo::Token::Number);
    CHECK(lexer.currentLiteral() == "10");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Number);
    CHECK(lexer.currentLiteral() == "20");
}

// =============================================================================
// Hyphen-in-identifier tests (Phase 6.1.3)
// =============================================================================

TEST_CASE("Lexer.hyphen_identifier_shell_mode")
{
    // In shell mode, hyphens are already part of identifiers
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("open-json"));
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "open-json");
}

TEST_CASE("Lexer.hyphen_identifier_fsharp_mode")
{
    // In F# mode, hyphen-letter should form compound identifiers.
    // Use "let x = open-json" so that the lexer enters F# mode naturally on `open-json`.
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("let x = open-json"));
    CHECK(lexer.currentToken() == endo::Token::Let); // let
    lexer.enterFSharpExpr();
    lexer.nextToken(); // x
    CHECK(lexer.currentLiteral() == "x");
    lexer.nextToken(); // =
    CHECK(lexer.currentToken() == endo::Token::Equal);
    lexer.nextToken(); // open-json (in F# mode, hyphen-letter forms compound id)
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "open-json");
}

TEST_CASE("Lexer.hyphen_identifier_fsharp_subtraction_with_spaces")
{
    // `a - b` in F# mode should be three tokens (subtraction)
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("let a - b"));
    CHECK(lexer.currentToken() == endo::Token::Let);
    lexer.enterFSharpExpr();
    lexer.nextToken(); // a
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "a");

    lexer.nextToken(); // -
    CHECK(lexer.currentToken() == endo::Token::Minus);

    lexer.nextToken(); // b
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "b");
}

TEST_CASE("Lexer.hyphen_identifier_fsharp_ab")
{
    // `a-b` in F# mode: hyphen followed by letter → single identifier
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("let a-b"));
    CHECK(lexer.currentToken() == endo::Token::Let);
    lexer.enterFSharpExpr();
    lexer.nextToken(); // a-b
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "a-b");
}

TEST_CASE("Lexer.hyphen_identifier_fsharp_x_minus_1")
{
    // `x-1` in F# mode: hyphen followed by digit → not part of identifier
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("let x-1"));
    CHECK(lexer.currentToken() == endo::Token::Let);
    lexer.enterFSharpExpr();
    lexer.nextToken(); // x
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "x");

    lexer.nextToken();
    // '-' followed by '1' in F# mode — the case '-' branch in nextToken handles this
    // It sees '-' then digit '1' → produces Number("-1")
    CHECK(lexer.currentToken() == endo::Token::Number);
    CHECK(lexer.currentLiteral() == "-1");
}

TEST_CASE("Lexer.hyphen_identifier_fsharp_multi_hyphen")
{
    // Multi-hyphen identifiers: `my-long-name`
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("let my-long-name"));
    CHECK(lexer.currentToken() == endo::Token::Let);
    lexer.enterFSharpExpr();
    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "my-long-name");
}

TEST_CASE("Lexer.hyphen_identifier_fsharp_from_csv")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("let from-csv"));
    CHECK(lexer.currentToken() == endo::Token::Let);
    lexer.enterFSharpExpr();
    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "from-csv");
}

TEST_CASE("Lexer.shell_mode_path_slash_single_token")
{
    // In shell mode (default), "projects/endo" is a single identifier
    // because '/' is NOT in ReservedSymbols.
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("cd projects/endo"));
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "cd");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "projects/endo");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::EndOfInput);
}

TEST_CASE("Lexer.fsharp_mode_path_slash_split_tokens")
{
    // In F# mode, "projects/endo" is split into "projects", "/", "endo"
    // because '/' IS in FSharpReservedSymbols and becomes Token::Slash.
    // The constructor lexes the first token, so we use "let" to trigger F# mode
    // before the path tokens are lexed.
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("let x = projects/endo"));
    CHECK(lexer.currentToken() == endo::Token::Let);
    lexer.enterFSharpExpr();

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "x");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Equal);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "projects");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Slash);

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::Identifier);
    CHECK(lexer.currentLiteral() == "endo");

    lexer.nextToken();
    CHECK(lexer.currentToken() == endo::Token::EndOfInput);
}
