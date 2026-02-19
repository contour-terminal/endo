// SPDX-License-Identifier: Apache-2.0
#include "ServerManager.hpp"

#include <format>
#include <print>

namespace endo::agent::mcp
{

ServerManager::ServerManager() = default;

ServerManager::~ServerManager()
{
    shutdown();
}

auto ServerManager::addServer(McpServerConfig const& config) -> McpVoidResult
{
    auto transport = std::make_unique<StdioTransport>();

    auto const transportConfig = StdioTransportConfig {
        .command = config.command,
        .args = config.args,
        .env = config.env,
    };

    auto startResult = transport->start(transportConfig);
    if (!startResult)
        return std::unexpected(startResult.error());

    auto client = std::make_unique<McpClient>(std::move(transport));

    auto initResult = client->initialize();
    if (!initResult)
        return std::unexpected(initResult.error());

    auto toolsResult = client->listTools();
    if (!toolsResult)
    {
        std::println(
            stderr, "MCP: Failed to list tools for server '{}': {}", config.name, toolsResult.error());
    }

    auto entry = ServerEntry {
        .name = config.name,
        .client = std::move(client),
        .tools = toolsResult ? std::move(*toolsResult) : std::vector<ToolDefinition> {},
    };

    auto const serverIdx = _servers.size();
    for (auto const& tool: entry.tools)
        _toolToServer[tool.name] = serverIdx;

    _servers.push_back(std::move(entry));
    return {};
}

auto ServerManager::allTools() const -> std::vector<ToolDefinition>
{
    auto result = std::vector<ToolDefinition> {};
    for (auto const& server: _servers)
    {
        for (auto const& tool: server.tools)
            result.push_back(tool);
    }
    return result;
}

auto ServerManager::callTool(std::string_view name, nlohmann::json const& arguments) -> McpResult<ToolResult>
{
    auto const it = _toolToServer.find(std::string(name));
    if (it == _toolToServer.end())
        return makeMcpError(McpErrorCode::ToolCallError, std::format("Unknown tool: {}", name));

    auto const serverIdx = it->second;
    if (serverIdx >= _servers.size())
        return makeMcpError(McpErrorCode::ToolCallError, "Server index out of range");

    auto result = _servers[serverIdx].client->callTool(name, arguments);
    if (result)
        result->callId = std::string(name);

    return result;
}

auto ServerManager::serverCount() const noexcept -> size_t
{
    return _servers.size();
}

void ServerManager::shutdown()
{
    _servers.clear();
    _toolToServer.clear();
}

} // namespace endo::agent::mcp
