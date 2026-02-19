// SPDX-License-Identifier: Apache-2.0
#include "McpToolAdapter.hpp"

#include <format>

namespace endo::agent::mcp
{

McpToolAdapter::McpToolAdapter(ServerManager& manager, ToolDefinition definition):
    _manager(manager), _definition(std::move(definition))
{
}

auto McpToolAdapter::name() const noexcept -> std::string_view
{
    return _definition.name;
}

auto McpToolAdapter::definition() const -> ToolDefinition
{
    return _definition;
}

auto McpToolAdapter::execute(nlohmann::json const& arguments) -> std::expected<ToolResult, ToolError>
{
    auto result = _manager.callTool(_definition.name, arguments);
    if (!result)
        return std::unexpected(ToolError { .message = std::format("MCP error: {}", result.error().message) });

    return std::move(*result);
}

} // namespace endo::agent::mcp
