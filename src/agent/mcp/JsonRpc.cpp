// SPDX-License-Identifier: Apache-2.0
#include "JsonRpc.hpp"

namespace endo::agent::mcp::jsonrpc
{

auto makeRequest(int64_t id, std::string_view method, nlohmann::json params) -> nlohmann::json
{
    auto msg = nlohmann::json {
        { "jsonrpc", "2.0" },
        { "id", id },
        { "method", method },
    };

    if (!params.is_null())
        msg["params"] = std::move(params);

    return msg;
}

auto makeNotification(std::string_view method, nlohmann::json params) -> nlohmann::json
{
    auto msg = nlohmann::json {
        { "jsonrpc", "2.0" },
        { "method", method },
    };

    if (!params.is_null())
        msg["params"] = std::move(params);

    return msg;
}

auto parseResponse(nlohmann::json const& message) -> McpResult<Response>
{
    if (!message.contains("jsonrpc") || message["jsonrpc"] != "2.0")
        return makeMcpError(McpErrorCode::ProtocolError, "Not a valid JSON-RPC 2.0 message");

    auto response = Response {};

    if (message.contains("id"))
        response.id = message["id"];

    if (message.contains("result"))
    {
        response.result = message["result"];
    }
    else if (message.contains("error"))
    {
        auto const& err = message["error"];
        response.error = RpcError {
            .code = err.value("code", 0),
            .message = err.value("message", std::string("Unknown error")),
            .data = err.value("data", nlohmann::json {}),
        };
    }
    else if (!message.contains("method"))
    {
        return makeMcpError(McpErrorCode::ProtocolError,
                            "JSON-RPC message has neither result, error, nor method");
    }

    return response;
}

} // namespace endo::agent::mcp::jsonrpc
