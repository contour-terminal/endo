// SPDX-License-Identifier: Apache-2.0
#include "LocalTcpListener.hpp"

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

#include <array>
#include <cstring>
#include <string>

namespace endo::http
{

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

LocalTcpListener::~LocalTcpListener()
{
    close();
}

LocalTcpListener::LocalTcpListener(LocalTcpListener&& other) noexcept: _listenFd(other._listenFd)
{
    other._listenFd = -1;
}

LocalTcpListener& LocalTcpListener::operator=(LocalTcpListener&& other) noexcept
{
    if (this != &other)
    {
        close();
        _listenFd = other._listenFd;
        other._listenFd = -1;
    }
    return *this;
}

auto LocalTcpListener::start() -> std::expected<uint16_t, std::string>
{
#if defined(_WIN32)
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

auto LocalTcpListener::acceptConnection(std::chrono::seconds timeout) -> std::expected<int, std::string>
{
    if (_listenFd < 0)
        return std::unexpected(std::string("Listener not started"));

#if !defined(_WIN32)
    auto pfd = pollfd { .fd = _listenFd, .events = POLLIN, .revents = 0 };
    auto const timeoutMs = static_cast<int>(timeout.count() * 1000);
    auto const pollResult = poll(&pfd, 1, timeoutMs);

    if (pollResult < 0)
        return std::unexpected(std::string("poll() failed: ") + strerror(errno));
    if (pollResult == 0)
        return std::unexpected(std::string("Timed out waiting for connection"));
#else
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(static_cast<SOCKET>(_listenFd), &readSet);
    auto tv = timeval {};
    tv.tv_sec = static_cast<long>(timeout.count());
    tv.tv_usec = 0;
    auto const selectResult = select(0, &readSet, nullptr, nullptr, &tv);
    if (selectResult <= 0)
        return std::unexpected(std::string("Timed out waiting for connection"));
#endif

    auto const clientFd = accept(_listenFd, nullptr, nullptr);
    if (clientFd < 0)
        return std::unexpected(std::string("Failed to accept connection: ") + strerror(errno));

    return clientFd;
}

auto LocalTcpListener::serveOnce(std::chrono::seconds timeout, std::string_view response)
    -> std::expected<void, std::string>
{
    auto clientFd = acceptConnection(timeout);
    if (!clientFd)
        return std::unexpected(clientFd.error());

    // Drain the incoming HTTP request.
    auto buffer = std::array<char, 4096> {};
    recv(*clientFd, buffer.data(), buffer.size() - 1, 0);

    // Send the canned response.
    send(*clientFd, response.data(), response.size(), 0);
    closeSocket(*clientFd);

    return {};
}

void LocalTcpListener::close()
{
    if (_listenFd >= 0)
    {
        closeSocket(_listenFd);
        _listenFd = -1;
    }
}

} // namespace endo::http
