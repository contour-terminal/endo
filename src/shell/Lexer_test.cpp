// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch.hpp>

#include <shell/Lexer.h>

TEST_CASE("Lexer.basic")
{
    auto lexer = crush::Lexer(std::make_unique<crush::StringSource>("echo hello world $PATH"));
    CHECK(lexer.currentToken() == crush::Token::Identifier);
    CHECK(lexer.currentLiteral() == "echo");

    lexer.nextToken();
    CHECK(lexer.currentToken() == crush::Token::Identifier);
    CHECK(lexer.currentLiteral() == "hello");

    lexer.nextToken();
    CHECK(lexer.currentToken() == crush::Token::Identifier);
    CHECK(lexer.currentLiteral() == "world");

    lexer.nextToken();
    CHECK(lexer.currentToken() == crush::Token::DollarName);
    CHECK(lexer.currentLiteral() == "PATH");

    lexer.nextToken();
    CHECK(lexer.currentToken() == crush::Token::EndOfInput);
}
