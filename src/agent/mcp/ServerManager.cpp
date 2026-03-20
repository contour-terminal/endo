// SPDX-License-Identifier: Apache-2.0
#include "ServerManager.hpp"

#include <algorithm>
#include <format>
#include <print>
#include <ranges>
#include <set>

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

auto ServerManager::addServerWithClient(std::string name, std::unique_ptr<McpClient> client) -> McpVoidResult
{
    auto toolsResult = client->listTools();
    if (!toolsResult)
    {
        std::println(stderr, "MCP: Failed to list tools for server '{}': {}", name, toolsResult.error());
    }

    auto entry = ServerEntry {
        .name = std::move(name),
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

auto ServerManager::refreshTools(std::string_view serverName) -> McpVoidResult
{
    auto const it =
        std::ranges::find_if(_servers, [&](auto const& entry) { return entry.name == serverName; });

    if (it == _servers.end())
        return makeMcpError(McpErrorCode::ToolCallError, std::format("Unknown server: {}", serverName));

    auto newToolsResult = it->client->listTools();
    if (!newToolsResult)
        return std::unexpected(newToolsResult.error());

    // Compute diff: old tool names vs new tool names.
    auto oldNames = std::set<std::string> {};
    for (auto const& tool: it->tools)
        oldNames.insert(tool.name);

    auto newNames = std::set<std::string> {};
    for (auto const& tool: *newToolsResult)
        newNames.insert(tool.name);

    auto removed = std::vector<std::string> {};
    for (auto const& name: oldNames)
    {
        if (!newNames.contains(name))
            removed.push_back(name);
    }

    auto added = std::vector<ToolDefinition> {};
    for (auto const& tool: *newToolsResult)
    {
        if (!oldNames.contains(tool.name))
            added.push_back(tool);
    }

    // Update the entry's tool list and rebuild routing.
    it->tools = std::move(*newToolsResult);
    rebuildToolIndex();

    // Notify callback.
    if (_toolsChangedCallback && (!added.empty() || !removed.empty()))
        _toolsChangedCallback(serverName, added, removed);

    return {};
}

void ServerManager::processNotifications()
{
    for (auto& server: _servers)
    {
        auto notifications = server.client->drainNotifications();
        for (auto const& notif: notifications)
        {
            if (notif.method == "notifications/tools/list_changed")
            {
                auto result = refreshTools(server.name);
                if (!result)
                    std::println(
                        stderr, "MCP: Failed to refresh tools for '{}': {}", server.name, result.error());
            }
        }
    }
}

void ServerManager::setToolsChangedCallback(ToolsChangedCallback callback)
{
    _toolsChangedCallback = std::move(callback);
}

void ServerManager::shutdown()
{
    _servers.clear();
    _toolToServer.clear();
}

void ServerManager::rebuildToolIndex()
{
    _toolToServer.clear();
    // macOS libc++ does not yet provide std::views::enumerate (C++23).
#if defined(__cpp_lib_ranges_enumerate) && __cpp_lib_ranges_enumerate >= 202302L
    for (auto const& [idx, server]: _servers | std::views::enumerate)
#else
    for (size_t idx = 0; auto const& server: _servers)
#endif
    {
        for (auto const& tool: server.tools)
            _toolToServer[tool.name] = idx;
#if !defined(__cpp_lib_ranges_enumerate) || __cpp_lib_ranges_enumerate < 202302L
        ++idx;
#endif
    }
}

} // namespace endo::agent::mcp
