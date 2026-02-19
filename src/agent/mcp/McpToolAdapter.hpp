// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <agent/mcp/ServerManager.hpp>
#include <agent/tools/AgentTool.hpp>

namespace endo::agent::mcp
{

/// @brief Bridges an MCP tool into the existing AgentTool/ToolRegistry system.
///
/// Each McpToolAdapter wraps a single MCP tool definition and delegates
/// execution to the ServerManager, which routes to the correct MCP server.
class McpToolAdapter final: public AgentTool
{
  public:
    /// @brief Constructs an adapter for an MCP tool.
    /// @param manager Reference to the ServerManager that owns the MCP connection.
    /// @param definition The tool definition from the MCP server.
    McpToolAdapter(ServerManager& manager, ToolDefinition definition);

    [[nodiscard]] auto name() const noexcept -> std::string_view override;
    [[nodiscard]] auto definition() const -> ToolDefinition override;
    [[nodiscard]] auto execute(nlohmann::json const& arguments)
        -> std::expected<ToolResult, ToolError> override;

  private:
    ServerManager& _manager;
    ToolDefinition _definition;
};

} // namespace endo::agent::mcp
