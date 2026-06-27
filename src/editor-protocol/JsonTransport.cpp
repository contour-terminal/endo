// SPDX-License-Identifier: Apache-2.0
#include "JsonTransport.hpp"

#include <charconv>
#include <format>

namespace endo::editor_protocol
{

std::expected<nlohmann::json, std::string> readMessage(std::istream& input)
{
    // Read headers until blank line (\r\n\r\n)
    int contentLength = -1;

    std::string line;
    while (std::getline(input, line))
    {
        // Remove trailing \r if present (getline strips \n but not \r)
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        // Empty line signals end of headers
        if (line.empty())
            break;

        // Parse Content-Length header
        constexpr std::string_view Prefix = "Content-Length: ";
        if (line.starts_with(Prefix))
        {
            auto const valueStr = std::string_view(line).substr(Prefix.size());
            auto const [ptr, ec] =
                std::from_chars(valueStr.data(), valueStr.data() + valueStr.size(), contentLength);
            if (ec != std::errc {})
                return std::unexpected(std::format("Invalid Content-Length value: {}", valueStr));
        }
        // Other headers (e.g., Content-Type) are ignored
    }

    if (input.fail() || input.eof())
        return std::unexpected(std::string("Connection closed"));

    if (contentLength < 0)
        return std::unexpected(std::string("Missing Content-Length header"));

    // Read exactly contentLength bytes
    std::string body(static_cast<size_t>(contentLength), '\0');
    input.read(body.data(), contentLength);

    if (input.gcount() != contentLength)
        return std::unexpected(
            std::format("Truncated body: expected {} bytes, got {}", contentLength, input.gcount()));

    // Parse JSON
    try
    {
        return nlohmann::json::parse(body);
    }
    catch (nlohmann::json::parse_error const& e)
    {
        return std::unexpected(std::format("JSON parse error: {}", e.what()));
    }
}

void writeMessage(std::ostream& output, nlohmann::json const& message)
{
    auto const body = message.dump();
    output << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    output.flush();
}

nlohmann::json makeResponse(nlohmann::json id, nlohmann::json result)
{
    return nlohmann::json {
        { "jsonrpc", "2.0" },
        { "id", std::move(id) },
        { "result", std::move(result) },
    };
}

nlohmann::json makeErrorResponse(nlohmann::json id, ErrorCode code, std::string const& message)
{
    return nlohmann::json {
        { "jsonrpc", "2.0" },
        { "id", std::move(id) },
        { "error",
          nlohmann::json {
              { "code", static_cast<int>(code) },
              { "message", message },
          } },
    };
}

nlohmann::json makeNotification(std::string const& method, nlohmann::json params)
{
    return nlohmann::json {
        { "jsonrpc", "2.0" },
        { "method", method },
        { "params", std::move(params) },
    };
}

} // namespace endo::editor_protocol
