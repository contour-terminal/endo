// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <agent/providers/local/ToolCallParser.hpp>

using namespace endo::agent;
using namespace endo::agent::local;

namespace
{
/// Creates a minimal set of tool definitions for testing.
auto makeTestTools() -> std::vector<ToolDefinition>
{
    return {
        ToolDefinition {
            .name = "read_file",
            .description = "Reads a file from disk.",
            .inputSchema = nlohmann::json { { "type", "object" },
                                            { "properties", { { "path", { { "type", "string" } } } } } },
        },
        ToolDefinition {
            .name = "write_file",
            .description = "Writes content to a file.",
            .inputSchema = nlohmann::json { { "type", "object" },
                                            { "properties",
                                              { { "path", { { "type", "string" } } },
                                                { "content", { { "type", "string" } } } } } },
        },
    };
}
} // namespace

TEST_CASE("agent.local.tool_call_parser.single_xml_tag", "[agent][local]")
{
    auto const tools = makeTestTools();
    const auto* const output =
        R"(<tool_call>{"name": "read_file", "arguments": {"path": "/tmp/test.txt"}}</tool_call>)";

    auto const result = parseToolCalls(output, tools);

    REQUIRE(result.toolCalls.size() == 1);
    CHECK(result.toolCalls[0].name == "read_file");
    CHECK(result.toolCalls[0].arguments["path"] == "/tmp/test.txt");
    CHECK_FALSE(result.toolCalls[0].id.empty());
    CHECK_FALSE(result.hadParsingErrors);
}

TEST_CASE("agent.local.tool_call_parser.multiple_xml_tags", "[agent][local]")
{
    auto const tools = makeTestTools();
    const auto* const output =
        R"(<tool_call>{"name": "read_file", "arguments": {"path": "/a.txt"}}</tool_call>)"
        R"(<tool_call>{"name": "write_file", "arguments": {"path": "/b.txt", "content": "hello"}}</tool_call>)";

    auto const result = parseToolCalls(output, tools);

    REQUIRE(result.toolCalls.size() == 2);
    CHECK(result.toolCalls[0].name == "read_file");
    CHECK(result.toolCalls[0].arguments["path"] == "/a.txt");
    CHECK(result.toolCalls[1].name == "write_file");
    CHECK(result.toolCalls[1].arguments["path"] == "/b.txt");
    CHECK(result.toolCalls[1].arguments["content"] == "hello");
    CHECK_FALSE(result.hadParsingErrors);
    // Each tool call should have a unique ID.
    CHECK(result.toolCalls[0].id != result.toolCalls[1].id);
}

TEST_CASE("agent.local.tool_call_parser.mixed_text_and_xml", "[agent][local]")
{
    auto const tools = makeTestTools();
    const auto* const output =
        "Let me read that file for you.\n"
        R"(<tool_call>{"name": "read_file", "arguments": {"path": "/tmp/test.txt"}}</tool_call>)"
        "\nDone processing.";

    auto const result = parseToolCalls(output, tools);

    REQUIRE(result.toolCalls.size() == 1);
    CHECK(result.toolCalls[0].name == "read_file");
    CHECK_FALSE(result.textContent.empty());
    CHECK(result.textContent.find("Let me read that file") != std::string::npos);
    CHECK(result.textContent.find("Done processing") != std::string::npos);
    CHECK_FALSE(result.hadParsingErrors);
}

TEST_CASE("agent.local.tool_call_parser.json_code_block", "[agent][local]")
{
    auto const tools = makeTestTools();
    const auto* const output = "Here is the tool call:\n"
                               "```json\n"
                               R"({"name": "read_file", "arguments": {"path": "/tmp/data.csv"}})"
                               "\n```";

    auto const result = parseToolCalls(output, tools);

    REQUIRE(result.toolCalls.size() == 1);
    CHECK(result.toolCalls[0].name == "read_file");
    CHECK(result.toolCalls[0].arguments["path"] == "/tmp/data.csv");
    CHECK_FALSE(result.hadParsingErrors);
}

TEST_CASE("agent.local.tool_call_parser.inline_json", "[agent][local]")
{
    auto const tools = makeTestTools();
    const auto* const output =
        R"(I will call {"name": "read_file", "arguments": {"path": "/tmp/file.txt"}} now.)";

    auto const result = parseToolCalls(output, tools);

    REQUIRE(result.toolCalls.size() == 1);
    CHECK(result.toolCalls[0].name == "read_file");
    CHECK(result.toolCalls[0].arguments["path"] == "/tmp/file.txt");
    CHECK_FALSE(result.hadParsingErrors);
}

TEST_CASE("agent.local.tool_call_parser.malformed_json", "[agent][local]")
{
    auto const tools = makeTestTools();
    const auto* const output = R"(<tool_call>{not valid json!!!}</tool_call>)";

    auto const result = parseToolCalls(output, tools);

    CHECK(result.toolCalls.empty());
    CHECK(result.hadParsingErrors);
}

TEST_CASE("agent.local.tool_call_parser.unknown_tool_name", "[agent][local]")
{
    auto const tools = makeTestTools();
    const auto* const output =
        R"(<tool_call>{"name": "delete_everything", "arguments": {"force": true}}</tool_call>)";

    auto const result = parseToolCalls(output, tools);

    // Unknown tools are still extracted; validation is the caller's concern.
    REQUIRE(result.toolCalls.size() == 1);
    CHECK(result.toolCalls[0].name == "delete_everything");
    CHECK(result.toolCalls[0].arguments["force"] == true);
    CHECK_FALSE(result.hadParsingErrors);
}

TEST_CASE("agent.local.tool_call_parser.plain_text_no_tool_calls", "[agent][local]")
{
    auto const tools = makeTestTools();
    const auto* const output = "This is just a regular response with no tool calls at all.";

    auto const result = parseToolCalls(output, tools);

    CHECK(result.toolCalls.empty());
    CHECK(result.textContent == output);
    CHECK_FALSE(result.hadParsingErrors);
}

TEST_CASE("agent.local.tool_call_parser.nested_json_arguments", "[agent][local]")
{
    auto const tools = makeTestTools();
    const auto* const output =
        R"(<tool_call>{"name": "write_file", "arguments": {"path": "/config.json", "content": "{\"key\": [1, 2, {\"nested\": true}]}"}}</tool_call>)";

    auto const result = parseToolCalls(output, tools);

    REQUIRE(result.toolCalls.size() == 1);
    CHECK(result.toolCalls[0].name == "write_file");
    CHECK(result.toolCalls[0].arguments["path"] == "/config.json");
    // The content argument itself is a string containing JSON.
    CHECK(result.toolCalls[0].arguments["content"].is_string());
    CHECK_FALSE(result.hadParsingErrors);
}
