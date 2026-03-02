// SPDX-License-Identifier: Apache-2.0
#include "OAuthCallbackServer.hpp"

#if !defined(_WIN32)
    #include <sys/socket.h>

    #include <poll.h>
    #include <unistd.h>

    #include <arpa/inet.h>
    #include <netinet/in.h>
#else
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#endif

#include <algorithm>
#include <array>
#include <cstring>
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

    void closeSocket(int fd)
    {
        if (fd < 0)
            return;
#if defined(_WIN32)
        closesocket(fd);
#else
        ::close(fd);
#endif
    }

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

OAuthCallbackServer::~OAuthCallbackServer()
{
    close();
}

OAuthCallbackServer::OAuthCallbackServer(OAuthCallbackServer&& other) noexcept: _listenFd(other._listenFd)
{
    other._listenFd = -1;
}

OAuthCallbackServer& OAuthCallbackServer::operator=(OAuthCallbackServer&& other) noexcept
{
    if (this != &other)
    {
        close();
        _listenFd = other._listenFd;
        other._listenFd = -1;
    }
    return *this;
}

auto OAuthCallbackServer::start() -> std::expected<uint16_t, std::string>
{
#if defined(_WIN32)
    // Initialize Winsock if needed.
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        return std::unexpected(std::string("WSAStartup failed"));
#endif

    _listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (_listenFd < 0)
        return std::unexpected(std::string("Failed to create socket: ") + strerror(errno));

    // Allow address reuse.
    int optval = 1;
#if defined(_WIN32)
    setsockopt(_listenFd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char const*>(&optval), sizeof(optval));
#else
    setsockopt(_listenFd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
#endif

    // Bind to 127.0.0.1:0 (OS picks an ephemeral port).
    auto addr = sockaddr_in {};
    addr.sin_family = AF_INET;
    addr.sin_port = 0;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(_listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        auto const msg = std::string("Failed to bind: ") + strerror(errno);
        close();
        return std::unexpected(msg);
    }

    if (listen(_listenFd, 1) < 0)
    {
        auto const msg = std::string("Failed to listen: ") + strerror(errno);
        close();
        return std::unexpected(msg);
    }

    // Retrieve the assigned port.
    auto boundAddr = sockaddr_in {};
    auto addrLen = static_cast<socklen_t>(sizeof(boundAddr));
    if (getsockname(_listenFd, reinterpret_cast<sockaddr*>(&boundAddr), &addrLen) < 0)
    {
        auto const msg = std::string("Failed to get port: ") + strerror(errno);
        close();
        return std::unexpected(msg);
    }

    return ntohs(boundAddr.sin_port);
}

auto OAuthCallbackServer::waitForCallback(std::chrono::seconds timeout)
    -> std::expected<OAuthCallback, std::string>
{
    if (_listenFd < 0)
        return std::unexpected(std::string("Server not started"));

    // Wait for an incoming connection with timeout using poll().
#if !defined(_WIN32)
    auto pfd = pollfd { .fd = _listenFd, .events = POLLIN, .revents = 0 };
    auto const timeoutMs = static_cast<int>(timeout.count() * 1000);
    auto const pollResult = poll(&pfd, 1, timeoutMs);

    if (pollResult < 0)
        return std::unexpected(std::string("poll() failed: ") + strerror(errno));
    if (pollResult == 0)
        return std::unexpected(std::string("Timed out waiting for browser callback"));
#else
    // On Windows, use select().
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(static_cast<SOCKET>(_listenFd), &readSet);
    auto tv = timeval {};
    tv.tv_sec = static_cast<long>(timeout.count());
    tv.tv_usec = 0;
    auto const selectResult = select(0, &readSet, nullptr, nullptr, &tv);
    if (selectResult <= 0)
        return std::unexpected(std::string("Timed out waiting for browser callback"));
#endif

    // Accept the connection.
    auto const clientFd = accept(_listenFd, nullptr, nullptr);
    if (clientFd < 0)
        return std::unexpected(std::string("Failed to accept connection: ") + strerror(errno));

    // Read the HTTP request (we only need the first line: GET /?code=...&state=... HTTP/1.1).
    auto buffer = std::array<char, 4096> {};
    auto const bytesRead = recv(clientFd, buffer.data(), buffer.size() - 1, 0);
    if (bytesRead <= 0)
    {
        closeSocket(clientFd);
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
    send(clientFd, httpResponse.data(), httpResponse.size(), 0);
    closeSocket(clientFd);

    if (code.empty())
        return std::unexpected(std::string("No authorization code in callback URL"));

    return OAuthCallback {
        .code = std::move(code),
        .state = std::move(state),
    };
}

void OAuthCallbackServer::close()
{
    if (_listenFd >= 0)
    {
        closeSocket(_listenFd);
        _listenFd = -1;
    }
}

} // namespace endo::agent
