// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <agent/Types.hpp>
#include <agent/mcp/McpError.hpp>
#include <agent/mcp/Transport.hpp>
#include <platform/MessageQueue.hpp>

namespace endo::agent::mcp
{

/// @brief MCP server capabilities reported during initialization.
struct McpServerCapabilities
{
    bool hasTools = false;
    bool hasResources = false;
    bool hasPrompts = false;
    std::string serverName;
    std::string serverVersion;
};

/// @brief A notification received from an MCP server.
struct McpNotification
{
    std::string method;    ///< Notification method name.
    nlohmann::json params; ///< Optional notification parameters.
};

/// @brief Client for the Model Context Protocol (MCP).
///
/// Handles the MCP lifecycle: initialize, list tools, call tools.
/// Spawns a background I/O thread that reads from the transport, classifying
/// incoming messages as responses (routed to the pending request) or
/// notifications (buffered for later draining).
class McpClient
{
  public:
    /// @brief Constructs an McpClient with the given transport.
    /// @param transport The transport to use for communication.
    explicit McpClient(std::unique_ptr<Transport> transport);
    ~McpClient();

    McpClient(McpClient const&) = delete;
    McpClient& operator=(McpClient const&) = delete;

    /// @brief Performs the MCP initialize handshake.
    /// @return The server's capabilities or an error.
    [[nodiscard]] auto initialize() -> McpResult<McpServerCapabilities>;

    /// @brief Lists available tools from the server.
    /// @return A vector of tool definitions or an error.
    [[nodiscard]] auto listTools() -> McpResult<std::vector<ToolDefinition>>;

    /// @brief Calls a tool on the server.
    /// @param name The tool name.
    /// @param arguments The tool arguments.
    /// @return The tool result or an error.
    [[nodiscard]] auto callTool(std::string_view name, nlohmann::json const& arguments)
        -> McpResult<ToolResult>;

    /// @brief Returns the server capabilities (valid after initialize).
    [[nodiscard]] auto capabilities() const -> McpServerCapabilities const&;

    /// @brief Returns true if the client has been initialized.
    [[nodiscard]] auto isInitialized() const -> bool;

    /// @brief Drains all buffered notifications received since the last drain.
    /// @return Notifications accumulated by the I/O thread.
    [[nodiscard]] auto drainNotifications() -> std::vector<McpNotification>;

  private:
    std::unique_ptr<Transport> _transport;
    McpServerCapabilities _capabilities;
    int64_t _nextId = 1;
    bool _initialized = false;

    /// @brief Queue for responses: I/O thread pushes, sendRequest() pops.
    platform::MessageQueue<McpResult<nlohmann::json>> _responseQueue;

    /// @brief Queue for notifications: I/O thread pushes, drainNotifications() drains.
    platform::MessageQueue<McpNotification> _notificationQueue;

    /// @brief Background I/O thread — reads from transport, classifies, and routes messages.
    /// Declared after the queues so it is destroyed (joined) before them.
    std::jthread _ioThread;

    /// @brief Background I/O loop — reads from transport, classifies, and routes messages.
    void ioLoop(std::stop_token const& stopToken);

    [[nodiscard]] auto sendRequest(std::string_view method, nlohmann::json params = nullptr)
        -> McpResult<nlohmann::json>;
};

} // namespace endo::agent::mcp
