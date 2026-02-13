// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

#include "SyntaxHighlighter.hpp"

using namespace endo;
using enum TokenCategory;

namespace
{

/// @brief Checks that all bytes in [start, start+len) have the expected category.
void expectRange(HighlightMap const& map, std::size_t start, std::size_t len, TokenCategory expected)
{
    for (std::size_t i = start; i < start + len && i < map.size(); ++i)
        CHECK(map[i] == expected);
}

} // namespace

TEST_CASE("SyntaxHighlighter.empty_input", "[SyntaxHighlighter]")
{
    auto const map = computeHighlightMap("");
    CHECK(map.empty());
}

TEST_CASE("SyntaxHighlighter.keyword_let", "[SyntaxHighlighter]")
{
    // "let x = 42"
    auto const map = computeHighlightMap("let x = 42");
    REQUIRE(map.size() == 10);
    expectRange(map, 0, 3, Keyword);  // "let"
    expectRange(map, 4, 1, Variable); // "x"
    expectRange(map, 6, 1, Operator); // "="
    expectRange(map, 8, 2, Number);   // "42"
}

TEST_CASE("SyntaxHighlighter.keyword_match", "[SyntaxHighlighter]")
{
    // "match x with"
    auto const map = computeHighlightMap("match x with");
    REQUIRE(map.size() == 12);
    expectRange(map, 0, 5, Keyword);  // "match"
    expectRange(map, 6, 1, Variable); // "x"
    expectRange(map, 8, 4, Keyword);  // "with"
}

TEST_CASE("SyntaxHighlighter.string_literal", "[SyntaxHighlighter]")
{
    // "\"hello\""
    auto const source = std::string("\"hello\"");
    auto const map = computeHighlightMap(source);
    REQUIRE(map.size() == source.size());
    // The entire quoted string should be highlighted as String
    for (std::size_t i = 0; i < map.size(); ++i)
    {
        INFO("byte[" << i << "] = '" << source[i] << "' category=" << static_cast<int>(map[i]));
        CHECK(map[i] == String);
    }
}

TEST_CASE("SyntaxHighlighter.single_quoted_string", "[SyntaxHighlighter]")
{
    // println 'hello' — single-quoted string including quotes must be fully highlighted
    auto const source = std::string("println 'hello'");
    auto const map = computeHighlightMap(source);
    REQUIRE(map.size() == source.size());
    // "println" is a keyword/variable, then space, then the entire 'hello' (including quotes) should be
    // String
    expectRange(map, 8, 7, String); // 'hello' = 7 bytes including quotes
}

TEST_CASE("SyntaxHighlighter.number_literal", "[SyntaxHighlighter]")
{
    auto const map = computeHighlightMap("123");
    REQUIRE(map.size() == 3);
    expectRange(map, 0, 3, Number);
}

TEST_CASE("SyntaxHighlighter.operators", "[SyntaxHighlighter]")
{
    // "1 + 2"
    auto const map = computeHighlightMap("1 + 2");
    REQUIRE(map.size() == 5);
    expectRange(map, 0, 1, Number);   // "1"
    expectRange(map, 2, 1, Operator); // "+"
    expectRange(map, 4, 1, Number);   // "2"
}

TEST_CASE("SyntaxHighlighter.forward_pipe", "[SyntaxHighlighter]")
{
    // "x |> print"
    auto const map = computeHighlightMap("x |> print");
    REQUIRE(map.size() == 10);
    expectRange(map, 0, 1, Variable); // "x"
    expectRange(map, 2, 2, Operator); // "|>"
    expectRange(map, 5, 5, Variable); // "print"
}

TEST_CASE("SyntaxHighlighter.constructors", "[SyntaxHighlighter]")
{
    // "Some 42"
    auto const map = computeHighlightMap("Some 42");
    REQUIRE(map.size() == 7);
    expectRange(map, 0, 4, Constructor); // "Some"
    expectRange(map, 5, 2, Number);      // "42"
}

TEST_CASE("SyntaxHighlighter.none_constructor", "[SyntaxHighlighter]")
{
    auto const map = computeHighlightMap("None");
    REQUIRE(map.size() == 4);
    expectRange(map, 0, 4, Constructor); // "None"
}

TEST_CASE("SyntaxHighlighter.fun_keyword", "[SyntaxHighlighter]")
{
    // "fun x -> x"
    auto const map = computeHighlightMap("fun x -> x");
    REQUIRE(map.size() == 10);
    expectRange(map, 0, 3, Keyword);  // "fun"
    expectRange(map, 4, 1, Variable); // "x"
    expectRange(map, 6, 2, Operator); // "->"
    expectRange(map, 9, 1, Variable); // "x"
}

TEST_CASE("SyntaxHighlighter.punctuation", "[SyntaxHighlighter]")
{
    // "(1, 2)"
    auto const map = computeHighlightMap("(1, 2)");
    REQUIRE(map.size() == 6);
    expectRange(map, 0, 1, Punctuation); // "("
    expectRange(map, 1, 1, Number);      // "1"
    expectRange(map, 2, 1, Punctuation); // ","
    expectRange(map, 4, 1, Number);      // "2"
    expectRange(map, 5, 1, Punctuation); // ")"
}

TEST_CASE("SyntaxHighlighter.whitespace_is_default", "[SyntaxHighlighter]")
{
    auto const map = computeHighlightMap("let  x");
    REQUIRE(map.size() == 6);
    expectRange(map, 0, 3, Keyword);  // "let"
    expectRange(map, 3, 2, Default);  // "  " (whitespace)
    expectRange(map, 5, 1, Variable); // "x"
}

// =============================================================================
// Context-aware tokenization tests
// =============================================================================

TEST_CASE("SyntaxHighlighter.shell_path_single_token", "[SyntaxHighlighter]")
{
    // "cd projects/endo" — shell command with path argument should keep the path as
    // a single token, not split by '/'.
    auto const map = computeHighlightMap("cd projects/endo");
    REQUIRE(map.size() == 16);
    expectRange(map, 0, 2, Function);  // "cd" (shell builtin)
    expectRange(map, 2, 1, Default);   // " "
    expectRange(map, 3, 13, Variable); // "projects/endo" (single token)
}

TEST_CASE("SyntaxHighlighter.shell_builtin_export", "[SyntaxHighlighter]")
{
    auto const map = computeHighlightMap("export FOO=bar");
    REQUIRE(map.size() == 14);
    expectRange(map, 0, 6, Function); // "export"
}

TEST_CASE("SyntaxHighlighter.shell_builtin_echo", "[SyntaxHighlighter]")
{
    auto const map = computeHighlightMap("echo hello world");
    REQUIRE(map.size() == 16);
    expectRange(map, 0, 4, Function); // "echo"
}

TEST_CASE("SyntaxHighlighter.fsharp_operators_preserved", "[SyntaxHighlighter]")
{
    // F# expression: operators should still be separate tokens.
    auto const map = computeHighlightMap("let x = 1 + 2");
    REQUIRE(map.size() == 13);
    expectRange(map, 0, 3, Keyword);   // "let"
    expectRange(map, 4, 1, Variable);  // "x"
    expectRange(map, 6, 1, Operator);  // "="
    expectRange(map, 8, 1, Number);    // "1"
    expectRange(map, 10, 1, Operator); // "+"
    expectRange(map, 12, 1, Number);   // "2"
}

TEST_CASE("SyntaxHighlighter.mixed_shell_and_fsharp_multiline", "[SyntaxHighlighter]")
{
    // Multi-line: first line is shell, second is F#.
    auto const source = std::string("cd /tmp\nlet x = 42");
    auto const map = computeHighlightMap(source);
    REQUIRE(map.size() == source.size());
    // Line 1: "cd /tmp"
    expectRange(map, 0, 2, Function); // "cd"
    // Line 2: "let x = 42" — starts at offset 8
    expectRange(map, 8, 3, Keyword);   // "let"
    expectRange(map, 12, 1, Variable); // "x"
    expectRange(map, 14, 1, Operator); // "="
    expectRange(map, 16, 2, Number);   // "42"
}

TEST_CASE("SyntaxHighlighter.non_builtin_shell_command", "[SyntaxHighlighter]")
{
    // A command that is not a known builtin should be Variable (not Function).
    auto const map = computeHighlightMap("git status");
    REQUIRE(map.size() == 10);
    expectRange(map, 0, 3, Variable); // "git" (regular identifier)
}

TEST_CASE("SyntaxHighlighter.function_color_distinct", "[SyntaxHighlighter]")
{
    auto const fnColor = categoryColor(Function);
    auto const kwColor = categoryColor(Keyword);
    auto const defColor = categoryColor(Default);
    CHECK(fnColor.r != kwColor.r);  // Function (blue) vs keyword (purple)
    CHECK(fnColor.r != defColor.r); // Function vs default
}

TEST_CASE("SyntaxHighlighter.categoryColor_returns_distinct_colors", "[SyntaxHighlighter]")
{
    auto const kwColor = categoryColor(Keyword);
    auto const defColor = categoryColor(Default);
    auto const numColor = categoryColor(Number);
    auto const strColor = categoryColor(String);

    // Each category should have a distinct color from Default
    CHECK(kwColor.r != defColor.r);  // Purple vs white-ish
    CHECK(numColor.r != defColor.r); // Orange vs white-ish
    CHECK(strColor.g != defColor.g); // Green channel differs
    CHECK(strColor.r != defColor.r); // Red channel differs

    // Keywords should be purple-ish (high red, low green)
    CHECK(static_cast<int>(kwColor.r) > static_cast<int>(kwColor.g));
}
