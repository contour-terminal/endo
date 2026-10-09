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
#include <type_traits>

namespace endo::http
{

#if defined(_WIN32)
static_assert(std::is_same_v<SocketHandle, SOCKET>, "SocketHandle must be Winsock's SOCKET");
static_assert(InvalidSocket == INVALID_SOCKET);
#endif

void closeSocket(SocketHandle handle)
{
    if (handle == InvalidSocket)
        return;
#if defined(_WIN32)
    closesocket(handle);
#else
    ::close(handle);
#endif
}

auto receiveFromSocket(SocketHandle handle, std::span<char> buffer) -> std::ptrdiff_t
{
#if defined(_WIN32)
    // Winsock takes the length as an int.
    return recv(handle, buffer.data(), static_cast<int>(buffer.size()), 0);
#else
    return recv(handle, buffer.data(), buffer.size(), 0);
#endif
}

auto sendToSocket(SocketHandle handle, std::string_view data) -> std::ptrdiff_t
{
#if defined(_WIN32)
    // Winsock takes the length as an int.
    return send(handle, data.data(), static_cast<int>(data.size()), 0);
#else
    return send(handle, data.data(), data.size(), 0);
#endif
}

LocalTcpListener::~LocalTcpListener()
{
    close();
}

LocalTcpListener::LocalTcpListener(LocalTcpListener&& other) noexcept: _listenSocket(other._listenSocket)
{
    other._listenSocket = InvalidSocket;
}

LocalTcpListener& LocalTcpListener::operator=(LocalTcpListener&& other) noexcept
{
    if (this != &other)
    {
        close();
        _listenSocket = other._listenSocket;
        other._listenSocket = InvalidSocket;
    }
    return *this;
}

auto LocalTcpListener::start() -> std::expected<uint16_t, std::string>
{
    // Close any previously open listener to prevent socket leaks on repeated calls.
    close();

#if defined(_WIN32)
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        return std::unexpected(std::string("WSAStartup failed"));
#endif

    _listenSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (_listenSocket == InvalidSocket)
        return std::unexpected(std::string("Failed to create socket: ") + strerror(errno));

    // Allow address reuse.
    int optval = 1;
#if defined(_WIN32)
    setsockopt(
        _listenSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char const*>(&optval), sizeof(optval));
#else
    setsockopt(_listenSocket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
#endif

    // Bind to 127.0.0.1:0 (OS picks an ephemeral port).
    auto addr = sockaddr_in {};
    addr.sin_family = AF_INET;
    addr.sin_port = 0;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(_listenSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        auto const msg = std::string("Failed to bind: ") + strerror(errno);
        close();
        return std::unexpected(msg);
    }

    if (listen(_listenSocket, 1) < 0)
    {
        auto const msg = std::string("Failed to listen: ") + strerror(errno);
        close();
        return std::unexpected(msg);
    }

    // Retrieve the assigned port.
    auto boundAddr = sockaddr_in {};
    auto addrLen = static_cast<socklen_t>(sizeof(boundAddr));
    if (getsockname(_listenSocket, reinterpret_cast<sockaddr*>(&boundAddr), &addrLen) < 0)
    {
        auto const msg = std::string("Failed to get port: ") + strerror(errno);
        close();
        return std::unexpected(msg);
    }

    return ntohs(boundAddr.sin_port);
}

auto LocalTcpListener::acceptConnection(std::chrono::seconds timeout)
    -> std::expected<SocketHandle, std::string>
{
    if (_listenSocket == InvalidSocket)
        return std::unexpected(std::string("Listener not started"));

#if !defined(_WIN32)
    auto pfd = pollfd { .fd = _listenSocket, .events = POLLIN, .revents = 0 };
    auto const timeoutMs = static_cast<int>(timeout.count() * 1000);
    auto const pollResult = poll(&pfd, 1, timeoutMs);

    if (pollResult < 0)
        return std::unexpected(std::string("poll() failed: ") + strerror(errno));
    if (pollResult == 0)
        return std::unexpected(std::string("Timed out waiting for connection"));
#else
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(_listenSocket, &readSet);
    auto tv = timeval {};
    tv.tv_sec = static_cast<long>(timeout.count());
    tv.tv_usec = 0;
    auto const selectResult = select(0, &readSet, nullptr, nullptr, &tv);
    if (selectResult <= 0)
        return std::unexpected(std::string("Timed out waiting for connection"));
#endif

    auto const clientSocket = accept(_listenSocket, nullptr, nullptr);
    if (clientSocket == InvalidSocket)
        return std::unexpected(std::string("Failed to accept connection: ") + strerror(errno));

    return clientSocket;
}

auto LocalTcpListener::serveOnce(std::chrono::seconds timeout, std::string_view response)
    -> std::expected<void, std::string>
{
    auto clientSocket = acceptConnection(timeout);
    if (!clientSocket)
        return std::unexpected(clientSocket.error());

    // Drain the incoming HTTP request.
    auto buffer = std::array<char, 4096> {};
    auto const bytesRead = receiveFromSocket(*clientSocket, std::span(buffer.data(), buffer.size() - 1));
    if (bytesRead <= 0)
    {
        closeSocket(*clientSocket);
        return std::unexpected(std::string("Failed to read from client"));
    }

    // Send the canned response.
    auto const bytesSent = sendToSocket(*clientSocket, response);
    closeSocket(*clientSocket);
    if (bytesSent < 0)
        return std::unexpected(std::string("Failed to send response"));

    return {};
}

void LocalTcpListener::close()
{
    if (_listenSocket != InvalidSocket)
    {
        closeSocket(_listenSocket);
        _listenSocket = InvalidSocket;
    }
}

} // namespace endo::http
