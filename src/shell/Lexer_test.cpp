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
    CHECK(lexer.currentToken() == endo::Token::String);
    CHECK(lexer.currentLiteral() == "\xE4\xB8\xAD\xE6\x96\x87");
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
    CHECK(lexer.currentToken() == endo::Token::Invalid);
}
