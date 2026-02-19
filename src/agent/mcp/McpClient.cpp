// SPDX-License-Identifier: Apache-2.0
#include "McpClient.hpp"

#include <format>

#include <agent/mcp/JsonRpc.hpp>

namespace endo::agent::mcp
{

McpClient::McpClient(std::unique_ptr<Transport> transport): _transport(std::move(transport))
{
}

McpClient::~McpClient() = default;

auto McpClient::initialize() -> McpResult<McpServerCapabilities>
{
    auto params = nlohmann::json {
        { "protocolVersion", "2024-11-05" },
        { "capabilities", nlohmann::json::object() },
        { "clientInfo",
          nlohmann::json {
              { "name", "endo" },
              { "version", "0.1.0" },
          } },
    };

    return sendRequest("initialize", std::move(params))
        .and_then([this](nlohmann::json const& result) -> McpResult<McpServerCapabilities> {
            auto const serverInfo = result.value("serverInfo", nlohmann::json {});
            _capabilities.serverName = serverInfo.value("name", std::string("unknown"));
            _capabilities.serverVersion = serverInfo.value("version", std::string("unknown"));

            if (result.contains("capabilities"))
            {
                auto const& caps = result["capabilities"];
                _capabilities.hasTools = caps.contains("tools");
                _capabilities.hasResources = caps.contains("resources");
                _capabilities.hasPrompts = caps.contains("prompts");
            }

            // Send initialized notification
            auto notif = jsonrpc::makeNotification("notifications/initialized");
            (void) _transport->send(notif);

            _initialized = true;
            return _capabilities;
        });
}

auto McpClient::listTools() -> McpResult<std::vector<ToolDefinition>>
{
    if (!_initialized)
        return makeMcpError(McpErrorCode::ProtocolError, "Client not initialized");

    return sendRequest("tools/list")
        .and_then([](nlohmann::json const& result) -> McpResult<std::vector<ToolDefinition>> {
            auto tools = std::vector<ToolDefinition> {};

            if (!result.contains("tools") || !result["tools"].is_array())
                return tools;

            for (auto const& toolJson: result["tools"])
            {
                auto tool = ToolDefinition {
                    .name = toolJson.value("name", ""),
                    .description = toolJson.value("description", ""),
                    .inputSchema = toolJson.value("inputSchema", nlohmann::json::object()),
                };
                tools.push_back(std::move(tool));
            }

            return tools;
        });
}

auto McpClient::callTool(std::string_view name, nlohmann::json const& arguments) -> McpResult<ToolResult>
{
    if (!_initialized)
        return makeMcpError(McpErrorCode::ProtocolError, "Client not initialized");

    auto params = nlohmann::json {
        { "name", name },
        { "arguments", arguments },
    };

    return sendRequest("tools/call", std::move(params))
        .and_then([&name](nlohmann::json const& result) -> McpResult<ToolResult> {
            auto toolResult = ToolResult {};
            toolResult.isError = result.value("isError", false);

            if (result.contains("content") && result["content"].is_array())
            {
                for (auto const& item: result["content"])
                {
                    if (item.value("type", "") == "text")
                    {
                        if (!toolResult.content.empty())
                            toolResult.content += "\n";
                        toolResult.content += item.value("text", "");
                    }
                }
            }

            return toolResult;
        });
}

auto McpClient::capabilities() const -> McpServerCapabilities const&
{
    return _capabilities;
}

auto McpClient::isInitialized() const -> bool
{
    return _initialized;
}

auto McpClient::sendRequest(std::string_view method, nlohmann::json params) -> McpResult<nlohmann::json>
{
    auto const id = _nextId++;
    auto request = jsonrpc::makeRequest(id, method, std::move(params));

    return _transport->send(request)
        .and_then([this]() -> McpResult<nlohmann::json> { return _transport->receive(); })
        .and_then([](nlohmann::json const& msg) -> McpResult<nlohmann::json> {
            return jsonrpc::parseResponse(msg).and_then(
                [](jsonrpc::Response const& resp) -> McpResult<nlohmann::json> {
                    if (resp.error)
                    {
                        return makeMcpError(
                            McpErrorCode::ProtocolError,
                            std::format("RPC error {}: {}", resp.error->code, resp.error->message));
                    }
                    return resp.result.value_or(nlohmann::json::object());
                });
        });
}

} // namespace endo::agent::mcp
