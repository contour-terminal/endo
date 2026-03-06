// SPDX-License-Identifier: Apache-2.0
#include "McpClient.hpp"

#include <format>

#include <agent/mcp/JsonRpc.hpp>

namespace endo::agent::mcp
{

McpClient::McpClient(std::unique_ptr<Transport> transport):
    _transport(std::move(transport)), _ioThread([this](std::stop_token const& st) { ioLoop(st); })
{
}

McpClient::~McpClient()
{
    _ioThread.request_stop();
    _transport->close(); // Unblocks the I/O thread's receive() call.
    _responseQueue.shutdown();
    _notificationQueue.shutdown();
    // jthread destructor joins.
}

void McpClient::ioLoop(std::stop_token const& stopToken)
{
    while (!stopToken.stop_requested())
    {
        auto result = _transport->receive(); // Blocking.
        if (!result.has_value())
        {
            // Transport error (closed, EOF) — push error and exit.
            _responseQueue.push(std::unexpected(result.error()));
            return;
        }

        auto const& msg = *result;

        if (jsonrpc::isNotification(msg))
        {
            // Notification: route to notification queue.
            _notificationQueue.push(McpNotification {
                .method = msg.value("method", std::string {}),
                .params = msg.value("params", nlohmann::json {}),
            });
        }
        else
        {
            // Response: route to response queue.
            _responseQueue.push(std::move(result));
        }
    }
}

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
            auto const sendResult [[maybe_unused]] = _transport->send(notif);

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

auto McpClient::drainNotifications() -> std::vector<McpNotification>
{
    auto result = std::vector<McpNotification> {};
    _notificationQueue.drainTo(result);
    return result;
}

auto McpClient::sendRequest(std::string_view method, nlohmann::json params) -> McpResult<nlohmann::json>
{
    auto const id = _nextId++;
    auto request = jsonrpc::makeRequest(id, method, std::move(params));

    auto sendResult = _transport->send(request);
    if (!sendResult)
        return std::unexpected(sendResult.error());

    // Block until the I/O thread delivers a response.
    auto response = _responseQueue.pop();
    if (!response.has_value())
        return makeMcpError(McpErrorCode::TransportError, "I/O thread shut down");

    // response is McpResult<nlohmann::json> — unwrap and parse.
    return response->and_then([](nlohmann::json const& respMsg) -> McpResult<nlohmann::json> {
        return jsonrpc::parseResponse(respMsg).and_then(
            [](jsonrpc::Response const& resp) -> McpResult<nlohmann::json> {
                if (resp.error)
                    return makeMcpError(
                        McpErrorCode::ProtocolError,
                        std::format("RPC error {}: {}", resp.error->code, resp.error->message));
                return resp.result.value_or(nlohmann::json::object());
            });
    });
}

} // namespace endo::agent::mcp
