// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <agent/mcp/McpToolAdapter.hpp>
#include <agent/mcp/ServerManager.hpp>

using namespace endo::agent;
using namespace endo::agent::mcp;

TEST_CASE("McpToolAdapter.name_and_definition", "[mcp][adapter]")
{
    auto manager = ServerManager {};
    auto const toolDef = ToolDefinition {
        .name = "test_tool",
        .description = "A test tool",
        .inputSchema = nlohmann::json { { "type", "object" } },
    };

    auto adapter = McpToolAdapter(manager, toolDef);
    CHECK(adapter.name() == "test_tool");

    auto const def = adapter.definition();
    CHECK(def.name == "test_tool");
    CHECK(def.description == "A test tool");
    CHECK(def.inputSchema["type"] == "object");
}

TEST_CASE("McpToolAdapter.execute_unknown_tool", "[mcp][adapter]")
{
    // No servers registered, so callTool will fail
    auto manager = ServerManager {};
    auto const toolDef = ToolDefinition {
        .name = "missing_tool",
        .description = "Tool not on any server",
        .inputSchema = nlohmann::json::object(),
    };

    auto adapter = McpToolAdapter(manager, toolDef);
    auto result = adapter.execute({});

    REQUIRE(!result.has_value());
    CHECK(result.error().message.find("MCP error") != std::string::npos);
}
