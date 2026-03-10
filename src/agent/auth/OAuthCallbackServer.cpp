// SPDX-License-Identifier: Apache-2.0
#include "OAuthCallbackServer.hpp"

#include <http/LocalTcpListener.hpp>

#if !defined(_WIN32)
    #include <sys/socket.h>
#else
    #include <winsock2.h>
#endif

#include <array>
#include <string>

namespace endo::agent
{

namespace
{
    constexpr auto SuccessHtml = R"html(<!DOCTYPE html>
<html>
<head><title>Authentication Successful</title></head>
<body style="font-family: -apple-system, sans-serif; text-align: center; padding: 60px;">
<h1>Authentication Successful</h1>
<p>You can close this tab and return to the terminal.</p>
</body>
</html>)html";

    /// Parses a query parameter value from a URL or query string.
    /// @param url   The full URL or query string to search.
    /// @param param The parameter name (e.g. "code").
    /// @return The parameter value, or empty string if not found.
    auto parseQueryParam(std::string_view url, std::string_view param) -> std::string
    {
        // Find the query string start.
        auto queryStart = url.find('?');
        if (queryStart == std::string_view::npos)
            queryStart = 0;
        else
            ++queryStart;

        auto const query = url.substr(queryStart);
        auto const target = std::string(param) + "=";
        auto pos = query.find(target);

        // Make sure we match the full parameter name (not a suffix).
        while (pos != std::string_view::npos)
        {
            if (pos == 0 || query[pos - 1] == '&')
                break;
            pos = query.find(target, pos + 1);
        }

        if (pos == std::string_view::npos)
            return {};

        auto const valueStart = pos + target.size();
        auto const valueEnd = query.find('&', valueStart);
        auto const value = (valueEnd != std::string_view::npos)
                               ? query.substr(valueStart, valueEnd - valueStart)
                               : query.substr(valueStart);
        return std::string(value);
    }

} // namespace

auto OAuthCallbackServer::waitForCallback(std::chrono::seconds timeout)
    -> std::expected<OAuthCallback, std::string>
{
    auto clientFd = _listener.acceptConnection(timeout);
    if (!clientFd)
        return std::unexpected(clientFd.error());

    // Read the HTTP request (we only need the first line: GET /?code=...&state=... HTTP/1.1).
    auto buffer = std::array<char, 4096> {};
    auto const bytesRead = recv(*clientFd, buffer.data(), buffer.size() - 1, 0);
    if (bytesRead <= 0)
    {
        http::closeSocket(*clientFd);
        return std::unexpected(std::string("Failed to read from client"));
    }
    buffer[bytesRead] = '\0';

    auto const requestLine = std::string_view(buffer.data(), static_cast<size_t>(bytesRead));

    // Extract the URL from the GET request line.
    auto url = std::string_view {};
    if (requestLine.starts_with("GET "))
    {
        auto const urlStart = size_t(4);
        auto const urlEnd = requestLine.find(' ', urlStart);
        url = requestLine.substr(urlStart, urlEnd - urlStart);
    }

    // Parse query parameters.
    auto code = parseQueryParam(url, "code");
    auto state = parseQueryParam(url, "state");

    // Send HTML response to the browser.
    auto const httpResponse = std::string("HTTP/1.1 200 OK\r\n"
                                          "Content-Type: text/html; charset=utf-8\r\n"
                                          "Connection: close\r\n"
                                          "\r\n")
                              + SuccessHtml;
    send(*clientFd, httpResponse.data(), httpResponse.size(), 0);
    http::closeSocket(*clientFd);

    if (code.empty())
        return std::unexpected(std::string("No authorization code in callback URL"));

    return OAuthCallback {
        .code = std::move(code),
        .state = std::move(state),
    };
}

} // namespace endo::agent
