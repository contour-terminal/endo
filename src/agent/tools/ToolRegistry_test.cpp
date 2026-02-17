// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <agent/tools/ToolRegistry.hpp>

using namespace endo::agent;

namespace
{

/// Mock tool for testing the registry.
class MockTool final: public AgentTool
{
  public:
    std::string toolName;
    std::string resultContent = "mock result";
    bool shouldFail = false;

    explicit MockTool(std::string name): toolName(std::move(name)) {}

    [[nodiscard]] auto name() const noexcept -> std::string_view override { return toolName; }

    [[nodiscard]] auto definition() const -> ToolDefinition override
    {
        return ToolDefinition {
            .name = toolName,
            .description = "Mock tool for testing",
            .inputSchema = nlohmann::json { { "type", "object" } },
        };
    }

    [[nodiscard]] auto execute(nlohmann::json const& /*arguments*/)
        -> std::expected<ToolResult, ToolError> override
    {
        if (shouldFail)
            return std::unexpected(ToolError { .message = "mock failure" });

        return ToolResult { .content = resultContent, .isError = false };
    }
};

} // namespace

TEST_CASE("ToolRegistry.register_and_find", "[agent][tools]")
{
    auto registry = ToolRegistry {};
    registry.registerTool(std::make_unique<MockTool>("test_tool"));

    CHECK(registry.size() == 1);
    CHECK(registry.findTool("test_tool") != nullptr);
    CHECK(registry.findTool("nonexistent") == nullptr);
}

TEST_CASE("ToolRegistry.definitions", "[agent][tools]")
{
    auto registry = ToolRegistry {};
    registry.registerTool(std::make_unique<MockTool>("tool_a"));
    registry.registerTool(std::make_unique<MockTool>("tool_b"));

    auto const defs = registry.definitions();
    REQUIRE(defs.size() == 2);
    CHECK(defs[0].name == "tool_a");
    CHECK(defs[1].name == "tool_b");
}

TEST_CASE("ToolRegistry.execute_dispatches_to_tool", "[agent][tools]")
{
    auto registry = ToolRegistry {};

    auto tool = std::make_unique<MockTool>("my_tool");
    tool->resultContent = "hello from tool";
    registry.registerTool(std::move(tool));

    auto const call = ToolCall { .id = "call-1", .name = "my_tool", .arguments = {} };
    auto const result = registry.execute(call);

    CHECK(result.callId == "call-1");
    CHECK(result.content == "hello from tool");
    CHECK_FALSE(result.isError);
}

TEST_CASE("ToolRegistry.execute_unknown_tool", "[agent][tools]")
{
    auto registry = ToolRegistry {};

    auto const call = ToolCall { .id = "call-2", .name = "unknown_tool", .arguments = {} };
    auto const result = registry.execute(call);

    CHECK(result.callId == "call-2");
    CHECK(result.content.find("Unknown tool") != std::string::npos);
    CHECK(result.isError);
}

TEST_CASE("ToolRegistry.execute_tool_error", "[agent][tools]")
{
    auto registry = ToolRegistry {};

    auto tool = std::make_unique<MockTool>("failing_tool");
    tool->shouldFail = true;
    registry.registerTool(std::move(tool));

    auto const call = ToolCall { .id = "call-3", .name = "failing_tool", .arguments = {} };
    auto const result = registry.execute(call);

    CHECK(result.callId == "call-3");
    CHECK(result.content.find("mock failure") != std::string::npos);
    CHECK(result.isError);
}
