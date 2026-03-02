// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <expected>
#include <format>
#include <string>

namespace endo::agent::mcp
{

/// @brief Error codes for MCP operations.
enum class McpErrorCode : uint8_t
{
    TransportError,
    ProtocolError,
    ToolCallError,
    TimeoutError,
};

/// @brief Error information from an MCP operation.
struct McpError
{
    McpErrorCode code = McpErrorCode::TransportError;
    std::string message;
};

/// @brief Result type for MCP operations that return a value or an error.
/// @tparam T The success value type.
template <typename T>
using McpResult = std::expected<T, McpError>;

/// @brief Result type for MCP operations that return no value on success.
using McpVoidResult = std::expected<void, McpError>;

/// @brief Creates an unexpected McpError value for use with std::expected.
/// @param code The error code.
/// @param message A descriptive error message.
/// @return An unexpected McpError.
[[nodiscard]] inline auto makeMcpError(McpErrorCode code, std::string message) -> std::unexpected<McpError>
{
    return std::unexpected<McpError>(McpError { .code = code, .message = std::move(message) });
}

} // namespace endo::agent::mcp

template <>
struct std::formatter<endo::agent::mcp::McpError>: std::formatter<std::string>
{
    auto format(endo::agent::mcp::McpError const& error, auto& ctx) const
    {
        return std::formatter<std::string>::format(
            std::format("[{}] {}", static_cast<int>(error.code), error.message), ctx);
    }
};
