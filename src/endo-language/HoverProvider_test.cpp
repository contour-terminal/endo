// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <endo-language/HoverProvider.hpp>

using namespace endo;

TEST_CASE("HoverProvider.keyword_let", "[hover]")
{
    auto result = computeHover("let x = 42", SourcePosition { .line = 0, .character = 1 });
    REQUIRE(result.has_value());
    CHECK(result->markdownText.find("`let`") != std::string::npos);
    CHECK(result->range.has_value());
}

TEST_CASE("HoverProvider.keyword_match", "[hover]")
{
    auto result = computeHover("match x with", SourcePosition { .line = 0, .character = 2 });
    REQUIRE(result.has_value());
    CHECK(result->markdownText.find("`match`") != std::string::npos);
}

TEST_CASE("HoverProvider.keyword_fun", "[hover]")
{
    auto result = computeHover("fun x -> x + 1", SourcePosition { .line = 0, .character = 1 });
    REQUIRE(result.has_value());
    CHECK(result->markdownText.find("`fun`") != std::string::npos);
}

TEST_CASE("HoverProvider.keyword_rec", "[hover]")
{
    auto result = computeHover("let rec f n = n", SourcePosition { .line = 0, .character = 4 });
    REQUIRE(result.has_value());
    CHECK(result->markdownText.find("`rec`") != std::string::npos);
}

TEST_CASE("HoverProvider.constructor_Some", "[hover]")
{
    auto result = computeHover("Some 42", SourcePosition { .line = 0, .character = 1 });
    REQUIRE(result.has_value());
    CHECK(result->markdownText.find("`Some`") != std::string::npos);
    CHECK(result->markdownText.find("option") != std::string::npos);
}

TEST_CASE("HoverProvider.constructor_None", "[hover]")
{
    auto result = computeHover("None", SourcePosition { .line = 0, .character = 1 });
    REQUIRE(result.has_value());
    CHECK(result->markdownText.find("`None`") != std::string::npos);
}

TEST_CASE("HoverProvider.constructor_Ok", "[hover]")
{
    auto result = computeHover("Ok 1", SourcePosition { .line = 0, .character = 0 });
    REQUIRE(result.has_value());
    CHECK(result->markdownText.find("`Ok`") != std::string::npos);
    CHECK(result->markdownText.find("result") != std::string::npos);
}

TEST_CASE("HoverProvider.constructor_Error", "[hover]")
{
    auto result = computeHover("Error \"oops\"", SourcePosition { .line = 0, .character = 2 });
    REQUIRE(result.has_value());
    CHECK(result->markdownText.find("`Error`") != std::string::npos);
}

TEST_CASE("HoverProvider.operator_pipe", "[hover]")
{
    auto result = computeHover("x |> f", SourcePosition { .line = 0, .character = 2 });
    REQUIRE(result.has_value());
    CHECK(result->markdownText.find("`|>`") != std::string::npos);
}

TEST_CASE("HoverProvider.operator_arrow", "[hover]")
{
    auto result = computeHover("fun x -> x", SourcePosition { .line = 0, .character = 6 });
    REQUIRE(result.has_value());
    CHECK(result->markdownText.find("`->`") != std::string::npos);
}

TEST_CASE("HoverProvider.operator_starstar", "[hover]")
{
    auto result = computeHover("2 ** 3", SourcePosition { .line = 0, .character = 2 });
    REQUIRE(result.has_value());
    CHECK(result->markdownText.find("`**`") != std::string::npos);
}

TEST_CASE("HoverProvider.builtin_print", "[hover]")
{
    auto result = computeHover("print 42", SourcePosition { .line = 0, .character = 2 });
    REQUIRE(result.has_value());
    CHECK(result->markdownText.find("`print`") != std::string::npos);
    CHECK(result->markdownText.find("unit") != std::string::npos);
}

TEST_CASE("HoverProvider.builtin_println", "[hover]")
{
    auto result = computeHover("println 42", SourcePosition { .line = 0, .character = 3 });
    REQUIRE(result.has_value());
    CHECK(result->markdownText.find("`println`") != std::string::npos);
}

TEST_CASE("HoverProvider.builtin_true", "[hover]")
{
    auto result = computeHover("true", SourcePosition { .line = 0, .character = 1 });
    REQUIRE(result.has_value());
    CHECK(result->markdownText.find("`true`") != std::string::npos);
    CHECK(result->markdownText.find("bool") != std::string::npos);
}

TEST_CASE("HoverProvider.binding_function", "[hover]")
{
    auto result = computeHover("let add x y = x + y", SourcePosition { .line = 0, .character = 4 });
    REQUIRE(result.has_value());
    CHECK(result->markdownText.find("`add`") != std::string::npos);
    CHECK(result->markdownText.find("function") != std::string::npos);
}

TEST_CASE("HoverProvider.binding_variable", "[hover]")
{
    auto result = computeHover("let x = 42", SourcePosition { .line = 0, .character = 4 });
    REQUIRE(result.has_value());
    CHECK(result->markdownText.find("`x`") != std::string::npos);
    CHECK(result->markdownText.find("binding") != std::string::npos);
    CHECK(result->markdownText.find("42") != std::string::npos);
}

TEST_CASE("HoverProvider.binding_parameter", "[hover]")
{
    auto result = computeHover("let f x = x + 1", SourcePosition { .line = 0, .character = 6 });
    REQUIRE(result.has_value());
    CHECK(result->markdownText.find("`x`") != std::string::npos);
    CHECK(result->markdownText.find("parameter") != std::string::npos);
    CHECK(result->markdownText.find("`f`") != std::string::npos);
}

TEST_CASE("HoverProvider.no_hover_on_number_literal", "[hover]")
{
    auto result = computeHover("let x = 42", SourcePosition { .line = 0, .character = 8 });
    CHECK(!result.has_value());
}

TEST_CASE("HoverProvider.no_hover_between_tokens", "[hover]")
{
    // Position after all tokens
    auto result = computeHover("let x = 42", SourcePosition { .line = 0, .character = 20 });
    CHECK(!result.has_value());
}

TEST_CASE("HoverProvider.empty_source", "[hover]")
{
    auto result = computeHover("", SourcePosition { .line = 0, .character = 0 });
    CHECK(!result.has_value());
}
