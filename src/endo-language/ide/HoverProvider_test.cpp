// SPDX-License-Identifier: Apache-2.0
#include <endo-language/ide/HoverProvider.hpp>

#include <catch2/catch_test_macros.hpp>

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

// =============================================================================
// Record type hover tests
// =============================================================================

TEST_CASE("HoverProvider.binding_record_variable_shows_type", "[hover]")
{
    const auto* source = "type Person = { name: str; age: int }\nlet alice = { name = \"Alice\"; age = 30 }";
    // Hover on "alice" (line 1, character 4)
    auto result = computeHover(source, SourcePosition { .line = 1, .character = 4 });
    REQUIRE(result.has_value());
    CHECK(result->markdownText.find("`alice`") != std::string::npos);
    CHECK(result->markdownText.find("Person") != std::string::npos);
}

TEST_CASE("HoverProvider.binding_record_variable_shows_fields", "[hover]")
{
    const auto* source = "type Person = { name: str; age: int }\nlet alice = { name = \"Alice\"; age = 30 }";
    auto result = computeHover(source, SourcePosition { .line = 1, .character = 4 });
    REQUIRE(result.has_value());
    // Should show the type definition with field names and types
    CHECK(result->markdownText.find("name: str") != std::string::npos);
    CHECK(result->markdownText.find("age: int") != std::string::npos);
}

TEST_CASE("HoverProvider.binding_record_type_in_code_block", "[hover]")
{
    // The type detection works via RecordExpr typeName resolved by the parser
    const auto* source = "type Person = { name: str; age: int }\nlet alice = { name = \"Alice\"; age = 30 }";
    auto result = computeHover(source, SourcePosition { .line = 1, .character = 4 });
    REQUIRE(result.has_value());
    // Should show the type in the code block as well
    CHECK(result->markdownText.find("Person") != std::string::npos);
    CHECK(result->markdownText.find("let alice") != std::string::npos);
}

TEST_CASE("HoverProvider.binding_record_literal_preview", "[hover]")
{
    const auto* source = "type Person = { name: str; age: int }\nlet alice = { name = \"Alice\"; age = 30 }";
    auto result = computeHover(source, SourcePosition { .line = 1, .character = 4 });
    REQUIRE(result.has_value());
    // Should show the record literal value preview
    CHECK(result->markdownText.find("\"Alice\"") != std::string::npos);
    CHECK(result->markdownText.find("30") != std::string::npos);
}

TEST_CASE("HoverProvider.binding_anonymous_record", "[hover]")
{
    const auto* source = "let r = { x = 1 }";
    auto result = computeHover(source, SourcePosition { .line = 0, .character = 4 });
    REQUIRE(result.has_value());
    CHECK(result->markdownText.find("`r`") != std::string::npos);
    CHECK(result->markdownText.find("binding") != std::string::npos);
    // No type name should be shown for anonymous records — check it doesn't say ": `"
    // (The binding header should just say "binding" without a type annotation)
    auto const typePos = result->markdownText.find("binding : `");
    CHECK(typePos == std::string::npos);
}

// =============================================================================
// Record field access hover tests
// =============================================================================

TEST_CASE("HoverProvider.field_access_shows_field_type", "[hover]")
{
    const auto* source = "type Person = { name: str; age: int }\n"
                         "let alice = { name = \"Alice\"; age = 30 }\n"
                         "alice.age";
    // Hover on "age" in "alice.age" (line 2, character 6)
    auto result = computeHover(source, SourcePosition { .line = 2, .character = 6 });
    REQUIRE(result.has_value());
    CHECK(result->markdownText.find("`age`") != std::string::npos);
    CHECK(result->markdownText.find("`int`") != std::string::npos);
}

TEST_CASE("HoverProvider.field_access_shows_record_type", "[hover]")
{
    const auto* source = "type Person = { name: str; age: int }\n"
                         "let alice = { name = \"Alice\"; age = 30 }\n"
                         "alice.age";
    auto result = computeHover(source, SourcePosition { .line = 2, .character = 6 });
    REQUIRE(result.has_value());
    CHECK(result->markdownText.find("Person") != std::string::npos);
}

TEST_CASE("HoverProvider.field_access_shows_type_definition", "[hover]")
{
    const auto* source = "type Person = { name: str; age: int }\n"
                         "let alice = { name = \"Alice\"; age = 30 }\n"
                         "alice.age";
    auto result = computeHover(source, SourcePosition { .line = 2, .character = 6 });
    REQUIRE(result.has_value());
    CHECK(result->markdownText.find("```endo") != std::string::npos);
    // toString(TypePtr) renders "str" as "string"
    CHECK(result->markdownText.find("type Person = { name: string; age: int }") != std::string::npos);
}

TEST_CASE("HoverProvider.field_access_string_field", "[hover]")
{
    const auto* source = "type Person = { name: str; age: int }\n"
                         "let alice = { name = \"Alice\"; age = 30 }\n"
                         "alice.name";
    // Hover on "name" in "alice.name" (line 2, character 6)
    auto result = computeHover(source, SourcePosition { .line = 2, .character = 6 });
    REQUIRE(result.has_value());
    CHECK(result->markdownText.find("`name`") != std::string::npos);
    CHECK(result->markdownText.find("`string`") != std::string::npos);
}

TEST_CASE("HoverProvider.field_access_no_hover_without_dot", "[hover]")
{
    const auto* source = "type Person = { name: str; age: int }\n"
                         "let alice = { name = \"Alice\"; age = 30 }\n"
                         "age";
    // Hover on standalone "age" (line 2, character 0) — not a field access
    auto result = computeHover(source, SourcePosition { .line = 2, .character = 0 });
    // Should not show field access hover (no preceding dot)
    if (result.has_value())
        CHECK(result->markdownText.find("field of") == std::string::npos);
}
