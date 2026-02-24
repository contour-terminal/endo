// SPDX-License-Identifier: Apache-2.0
#include <endo-language/format/FormatConfig.hpp>
#include <endo-language/format/SourceFormatter.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace endo::format;

// ============================================================================
// Comment trivia collection
// ============================================================================

TEST_CASE("Lexer.comment_collection_shell", "[format]")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("42 # comment"), true);
    while (lexer.currentToken() != endo::Token::EndOfInput)
        lexer.nextToken();

    auto const& comments = lexer.comments();
    REQUIRE(comments.size() == 1);
    CHECK(comments[0].style == endo::CommentStyle::Shell);
    CHECK(comments[0].text == "# comment");
}

TEST_CASE("Lexer.comment_collection_cstyle", "[format]")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("42 // comment"), true);
    while (lexer.currentToken() != endo::Token::EndOfInput)
        lexer.nextToken();

    auto const& comments = lexer.comments();
    REQUIRE(comments.size() == 1);
    CHECK(comments[0].style == endo::CommentStyle::CStyle);
    CHECK(comments[0].text == "// comment");
}

TEST_CASE("Lexer.comment_collection_fsharp_block", "[format]")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("(* block comment *) 42"), true);
    while (lexer.currentToken() != endo::Token::EndOfInput)
        lexer.nextToken();

    auto const& comments = lexer.comments();
    REQUIRE(comments.size() == 1);
    CHECK(comments[0].style == endo::CommentStyle::FSharp);
    CHECK(comments[0].text == "(* block comment *)");
}

TEST_CASE("Lexer.comment_collection_disabled_by_default", "[format]")
{
    auto lexer = endo::Lexer(std::make_unique<endo::StringSource>("# comment\n42"));
    while (lexer.currentToken() != endo::Token::EndOfInput)
        lexer.nextToken();

    CHECK(lexer.comments().empty());
}

// ============================================================================
// FormatConfig
// ============================================================================

TEST_CASE("FormatConfig.defaults", "[format]")
{
    FormatConfig config;
    CHECK(config.indentWidth == 4);
    CHECK(config.useSpaces == true);
    CHECK(config.maxLineWidth == 100);
    CHECK(config.trailingNewline == true);
    CHECK(config.blankLinesBetweenTopLevel == 1);
    CHECK(config.indentString() == "    ");
}

TEST_CASE("FormatConfig.indentString_tabs", "[format]")
{
    FormatConfig config;
    config.useSpaces = false;
    CHECK(config.indentString() == "\t");
}

// ============================================================================
// SourceFormatter — basic formatting
// ============================================================================

TEST_CASE("SourceFormatter.simple_echo", "[format]")
{
    auto const result = SourceFormatter::format("echo hello world");
    CHECK(result == "echo hello world\n");
}

TEST_CASE("SourceFormatter.let_binding_simple", "[format]")
{
    auto const result = SourceFormatter::format("let x = 42");
    CHECK(result == "let x = 42\n");
}

TEST_CASE("SourceFormatter.let_function", "[format]")
{
    auto const result = SourceFormatter::format("let add x y = (x + y)");
    CHECK(result == "let add x y = (x + y)\n");
}

TEST_CASE("SourceFormatter.match_expression", "[format]")
{
    auto const result = SourceFormatter::format("let f x = match x with | 0 -> \"zero\" | _ -> \"other\"");
    // Match arms should be on separate lines
    CHECK(result.find("| 0 ->") != std::string::npos);
    CHECK(result.find("| _ ->") != std::string::npos);
}

TEST_CASE("SourceFormatter.comment_preservation", "[format]")
{
    auto const result = SourceFormatter::format("# this is a comment\nlet x = 42");
    INFO("Formatted result: [" << result << "]");
    CHECK(result.find("# this is a comment") != std::string::npos);
    CHECK(result.find("let x = 42") != std::string::npos);
}

TEST_CASE("SourceFormatter.trailing_newline", "[format]")
{
    auto const result = SourceFormatter::format("let x = 42");
    CHECK(!result.empty());
    CHECK(result.back() == '\n');
}

TEST_CASE("SourceFormatter.trailing_newline_disabled", "[format]")
{
    FormatConfig config;
    config.trailingNewline = false;
    auto const result = SourceFormatter::format("let x = 42", config);
    CHECK(!result.empty());
    // Should not have trailing newline
    CHECK(result.back() != '\n');
}

TEST_CASE("SourceFormatter.parse_failure_returns_original", "[format]")
{
    auto const source = "let let let ???";
    auto const result = SourceFormatter::format(source);
    CHECK(result == source);
}

// ============================================================================
// Idempotency
// ============================================================================

TEST_CASE("SourceFormatter.idempotency_simple", "[format]")
{
    auto const source = "let x = 42";
    auto const first = SourceFormatter::format(source);
    auto const second = SourceFormatter::format(first);
    CHECK(first == second);
}

TEST_CASE("SourceFormatter.idempotency_match", "[format]")
{
    auto const source = "let f x = match x with | 0 -> \"zero\" | _ -> \"other\"";
    auto const first = SourceFormatter::format(source);
    auto const second = SourceFormatter::format(first);
    CHECK(first == second);
}

TEST_CASE("SourceFormatter.idempotency_if_expr", "[format]")
{
    auto const source = "let f x = if x == 0 then \"zero\" else \"nonzero\"";
    auto const first = SourceFormatter::format(source);
    auto const second = SourceFormatter::format(first);
    CHECK(first == second);
}
