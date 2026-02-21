// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <agent/mcp/McpError.hpp>
#include <nlohmann/json.hpp>

namespace endo::agent::mcp::jsonrpc
{

/// @brief Represents a JSON-RPC 2.0 error.
struct RpcError
{
    int code = 0;
    std::string message;
    nlohmann::json data;
};

/// @brief Represents a parsed JSON-RPC 2.0 response.
struct Response
{
    nlohmann::json id;
    std::optional<nlohmann::json> result;
    std::optional<RpcError> error;

    /// @brief Returns true if this response indicates success.
    [[nodiscard]] auto isSuccess() const -> bool { return result.has_value(); }
};

/// @brief Builds a JSON-RPC 2.0 request message.
/// @param id The request ID.
/// @param method The method name.
/// @param params Optional parameters.
/// @return The JSON-RPC request object.
[[nodiscard]] auto makeRequest(int64_t id, std::string_view method, nlohmann::json params = nullptr)
    -> nlohmann::json;

/// @brief Builds a JSON-RPC 2.0 notification message (no id).
/// @param method The method name.
/// @param params Optional parameters.
/// @return The JSON-RPC notification object.
[[nodiscard]] auto makeNotification(std::string_view method, nlohmann::json params = nullptr)
    -> nlohmann::json;

/// @brief Parses a JSON-RPC 2.0 response.
/// @param message The JSON message to parse.
/// @return The parsed response or an McpError.
[[nodiscard]] auto parseResponse(nlohmann::json const& message) -> McpResult<Response>;

/// @brief Checks if a JSON-RPC message is a notification (has "method" but no "id").
/// @param message The JSON message to inspect.
/// @return true if the message is a notification.
[[nodiscard]] auto isNotification(nlohmann::json const& message) -> bool;

} // namespace endo::agent::mcp::jsonrpc
