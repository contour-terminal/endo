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
