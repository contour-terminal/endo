// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <editor-protocol/JsonTransport.hpp>

#include <nlohmann/json.hpp>

#include <sstream>
#include <string>
#include <vector>

namespace endo::editor_protocol
{

/// Creates a JSON-RPC message string with Content-Length header.
inline std::string makeRpcMessage(nlohmann::json const& msg)
{
    auto const body = msg.dump();
    std::ostringstream oss;
    oss << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    return oss.str();
}

/// Builds a JSON-RPC request message.
inline nlohmann::json sendRequest(std::string const& method, nlohmann::json const& params, int id = 1)
{
    return nlohmann::json { { "jsonrpc", "2.0" }, { "id", id }, { "method", method }, { "params", params } };
}

/// Builds a JSON-RPC notification message (no id).
inline nlohmann::json sendNotification(std::string const& method, nlohmann::json const& params)
{
    return nlohmann::json { { "jsonrpc", "2.0" }, { "method", method }, { "params", params } };
}

/// Reads all JSON-RPC messages from the output stream.
inline std::vector<nlohmann::json> readAllMessages(std::istringstream& output)
{
    std::vector<nlohmann::json> messages;
    while (output.good() && output.peek() != EOF)
    {
        auto msg = readMessage(output);
        if (msg.has_value())
            messages.push_back(std::move(*msg));
        else
            break;
    }
    return messages;
}

} // namespace endo::editor_protocol
