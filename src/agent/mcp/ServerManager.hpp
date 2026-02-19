// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <agent/Types.hpp>
#include <agent/mcp/McpClient.hpp>
#include <agent/mcp/McpError.hpp>
#include <agent/mcp/StdioTransport.hpp>

namespace endo::agent::mcp
{

/// @brief Configuration for a single MCP server.
struct McpServerConfig
{
    std::string name;
    std::string command;
    std::vector<std::string> args;
    std::map<std::string, std::string> env;
};

/// @brief Manages multiple MCP server connections and routes tool calls.
class ServerManager
{
  public:
    ServerManager();
    ~ServerManager();

    ServerManager(ServerManager const&) = delete;
    ServerManager& operator=(ServerManager const&) = delete;

    /// @brief Starts and initializes an MCP server.
    /// @param config The server configuration.
    /// @return Success or an error.
    [[nodiscard]] auto addServer(McpServerConfig const& config) -> McpVoidResult;

    /// @brief Lists all available tools from all connected servers.
    /// @return A vector of tool definitions.
    [[nodiscard]] auto allTools() const -> std::vector<ToolDefinition>;

    /// @brief Calls a tool by name, routing to the correct server.
    /// @param name The tool name.
    /// @param arguments The tool arguments.
    /// @return The tool result or an error.
    [[nodiscard]] auto callTool(std::string_view name, nlohmann::json const& arguments)
        -> McpResult<ToolResult>;

    /// @brief Returns the number of connected servers.
    [[nodiscard]] auto serverCount() const noexcept -> size_t;

    /// @brief Shuts down all servers.
    void shutdown();

  private:
    struct ServerEntry
    {
        std::string name;
        std::unique_ptr<McpClient> client;
        std::vector<ToolDefinition> tools;
    };

    std::vector<ServerEntry> _servers;
    std::map<std::string, size_t> _toolToServer; ///< Maps tool name to server index.
};

} // namespace endo::agent::mcp
