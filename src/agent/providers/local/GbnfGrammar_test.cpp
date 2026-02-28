// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include <agent/providers/local/GbnfGrammar.hpp>

using namespace endo::agent;
using namespace endo::agent::local;

TEST_CASE("agent.local.gbnf_grammar.jsonSchemaToGbnf_simple_string_property", "[agent][local][gbnf]")
{
    auto const schema = nlohmann::json {
        { "type", "object" },
        { "properties", { { "name", { { "type", "string" } } } } },
    };

    auto const grammar = jsonSchemaToGbnf(schema);
    CHECK(grammar.find("root") != std::string::npos);
    CHECK(grammar.find("root-name") != std::string::npos);
    CHECK(grammar.find("string") != std::string::npos);
}

TEST_CASE("agent.local.gbnf_grammar.jsonSchemaToGbnf_object_multiple_properties", "[agent][local][gbnf]")
{
    auto const schema = nlohmann::json {
        { "type", "object" },
        { "properties",
          { { "name", { { "type", "string" } } },
            { "age", { { "type", "integer" } } },
            { "active", { { "type", "boolean" } } } } },
    };

    auto const grammar = jsonSchemaToGbnf(schema);
    CHECK(grammar.find("root-name ::= string") != std::string::npos);
    CHECK(grammar.find("root-age ::= integer") != std::string::npos);
    CHECK(grammar.find("root-active ::= boolean") != std::string::npos);
    CHECK(grammar.find("root ::= \"{\"") != std::string::npos);
}

TEST_CASE("agent.local.gbnf_grammar.jsonSchemaToGbnf_enum_values", "[agent][local][gbnf]")
{
    auto const schema = nlohmann::json {
        { "enum", { "red", "green", "blue" } },
    };

    auto const grammar = jsonSchemaToGbnf(schema);
    CHECK(grammar.find("\"\\\"red\\\"\"") != std::string::npos);
    CHECK(grammar.find("\"\\\"green\\\"\"") != std::string::npos);
    CHECK(grammar.find("\"\\\"blue\\\"\"") != std::string::npos);
}

TEST_CASE("agent.local.gbnf_grammar.jsonSchemaToGbnf_nested_object", "[agent][local][gbnf]")
{
    auto const schema = nlohmann::json {
        { "type", "object" },
        { "properties",
          { { "address",
              { { "type", "object" },
                { "properties",
                  { { "street", { { "type", "string" } } }, { "zip", { { "type", "string" } } } } } } } } },
    };

    auto const grammar = jsonSchemaToGbnf(schema);
    CHECK(grammar.find("root-address-street ::= string") != std::string::npos);
    CHECK(grammar.find("root-address-zip ::= string") != std::string::npos);
    CHECK(grammar.find("root-address ::= \"{\"") != std::string::npos);
}

TEST_CASE("agent.local.gbnf_grammar.jsonSchemaToGbnf_array_of_strings", "[agent][local][gbnf]")
{
    auto const schema = nlohmann::json {
        { "type", "array" },
        { "items", { { "type", "string" } } },
    };

    auto const grammar = jsonSchemaToGbnf(schema);
    CHECK(grammar.find("root-items ::= string") != std::string::npos);
    CHECK(grammar.find("root ::= \"[\"") != std::string::npos);
    CHECK(grammar.find("\"]\"") != std::string::npos);
}

TEST_CASE("agent.local.gbnf_grammar.generateToolCallGrammar_single_tool", "[agent][local][gbnf]")
{
    auto const tools = std::vector<ToolDefinition> {
        { .name = "read_file",
          .description = "Reads a file",
          .inputSchema = { { "type", "object" },
                           { "properties", { { "path", { { "type", "string" } } } } } } },
    };

    auto const grammar = generateToolCallGrammar(tools);
    CHECK(grammar.find("tool-call") != std::string::npos);
    CHECK(grammar.find("tool0-json") != std::string::npos);
    CHECK(grammar.find("tool0-args") != std::string::npos);
    CHECK(grammar.find("read_file") != std::string::npos);
    CHECK(grammar.find("<tool_call>") != std::string::npos);
    CHECK(grammar.find("</tool_call>") != std::string::npos);
}

TEST_CASE("agent.local.gbnf_grammar.generateToolCallGrammar_multiple_tools", "[agent][local][gbnf]")
{
    auto const tools = std::vector<ToolDefinition> {
        { .name = "read_file",
          .description = "Reads a file",
          .inputSchema = { { "type", "object" },
                           { "properties", { { "path", { { "type", "string" } } } } } } },
        { .name = "write_file",
          .description = "Writes a file",
          .inputSchema = { { "type", "object" },
                           { "properties",
                             { { "path", { { "type", "string" } } },
                               { "content", { { "type", "string" } } } } } } },
    };

    auto const grammar = generateToolCallGrammar(tools);
    CHECK(grammar.find("tool0-json") != std::string::npos);
    CHECK(grammar.find("tool1-json") != std::string::npos);
    CHECK(grammar.find("tool-json ::= tool0-json | tool1-json") != std::string::npos);
    CHECK(grammar.find("read_file") != std::string::npos);
    CHECK(grammar.find("write_file") != std::string::npos);
}

TEST_CASE("agent.local.gbnf_grammar.generateToolCallGrammar_contains_root_rule", "[agent][local][gbnf]")
{
    auto const tools = std::vector<ToolDefinition> {
        { .name = "test_tool",
          .description = "A test tool",
          .inputSchema = { { "type", "object" },
                           { "properties", { { "input", { { "type", "string" } } } } } } },
    };

    auto const grammar = generateToolCallGrammar(tools);
    CHECK(grammar.find("root ::=") != std::string::npos);
}

TEST_CASE("agent.local.gbnf_grammar.generateToolCallGrammar_contains_ws_rule", "[agent][local][gbnf]")
{
    auto const tools = std::vector<ToolDefinition> {
        { .name = "test_tool",
          .description = "A test tool",
          .inputSchema = { { "type", "object" },
                           { "properties", { { "input", { { "type", "string" } } } } } } },
    };

    auto const grammar = generateToolCallGrammar(tools);
    CHECK(grammar.find("ws ::=") != std::string::npos);
}
