// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <expected>
#include <istream>
#include <ostream>
#include <string>

#include <nlohmann/json.hpp>

namespace endo::editor_protocol
{

/// JSON-RPC 2.0 standard error codes.
enum class ErrorCode : int
{
    ParseError = -32700,
    InvalidRequest = -32600,
    MethodNotFound = -32601,
    InvalidParams = -32602,
    InternalError = -32603,
    ServerNotInitialized = -32002,
    RequestCancelled = -32800,
};

/// Reads a single JSON-RPC message from the input stream.
///
/// Parses `Content-Length: N\r\n\r\n` header followed by N bytes of JSON body.
/// @param input The input stream to read from
/// @return The parsed JSON message, or an error string on failure
[[nodiscard]] std::expected<nlohmann::json, std::string> readMessage(std::istream& input);

/// Writes a JSON-RPC message to the output stream.
///
/// Prepends `Content-Length: <len>\r\n\r\n` header and flushes.
/// @param output The output stream to write to
/// @param message The JSON message to send
void writeMessage(std::ostream& output, nlohmann::json const& message);

/// Creates a JSON-RPC 2.0 success response.
/// @param id The request ID
/// @param result The result payload
/// @return The complete JSON-RPC response object
[[nodiscard]] nlohmann::json makeResponse(nlohmann::json id, nlohmann::json result);

/// Creates a JSON-RPC 2.0 error response.
/// @param id The request ID
/// @param code The error code
/// @param message A human-readable error description
/// @return The complete JSON-RPC error response object
[[nodiscard]] nlohmann::json makeErrorResponse(nlohmann::json id, ErrorCode code, std::string const& message);

/// Creates a JSON-RPC 2.0 notification (no id field).
/// @param method The notification method name
/// @param params The notification parameters
/// @return The complete JSON-RPC notification object
[[nodiscard]] nlohmann::json makeNotification(std::string const& method, nlohmann::json params);

} // namespace endo::editor_protocol
