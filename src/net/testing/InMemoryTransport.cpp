// SPDX-License-Identifier: Apache-2.0

// clang-format off
#if defined(_WIN32)
    #include <winsock2.h>
    #include <windows.h>
    #include <ws2tcpip.h>
#endif
// clang-format on

#include <net/testing/InMemoryTransport.hpp>

#if !defined(_WIN32)
    #include <array>
    #include <cerrno>

    #include <sys/socket.h>

    #include <fcntl.h>
    #include <unistd.h>

    #include <net/posix/PosixSocket.hpp>
#else
    #include <net/windows/WindowsSocket.hpp>
#endif

namespace endo::net::testing
{

#if !defined(_WIN32)

std::expected<SocketPair, NetError> makeSocketPair(tui::runtime::TuiRuntime& runtime)
{
    auto fds = std::array<int, 2> {};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()) != 0)
        return std::unexpected(makeNetError(NetErrorCode::Other, errno, "socketpair"));

    for (auto const fd: fds)
    {
        auto const flags = ::fcntl(fd, F_GETFL, 0);
        if (flags >= 0)
            ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    return SocketPair {
        .first = std::unique_ptr<ISocket>(new PosixSocket(runtime, fds[0])),
        .second = std::unique_ptr<ISocket>(new PosixSocket(runtime, fds[1])),
    };
}

#else // _WIN32

namespace
{
    /// Creates a connected loopback TCP socket pair (Windows lacks socketpair).
    [[nodiscard]] bool makeLoopbackPair(SOCKET& a, SOCKET& b) noexcept
    {
        auto listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener == INVALID_SOCKET)
            return false;

        sockaddr_in addr {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;

        int len = sizeof(addr);
        if (::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR
            || ::listen(listener, 1) == SOCKET_ERROR
            || ::getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &len) == SOCKET_ERROR)
        {
            closesocket(listener);
            return false;
        }

        auto client = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (client == INVALID_SOCKET)
        {
            closesocket(listener);
            return false;
        }
        if (::connect(client, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR)
        {
            closesocket(client);
            closesocket(listener);
            return false;
        }
        auto server = ::accept(listener, nullptr, nullptr);
        closesocket(listener);
        if (server == INVALID_SOCKET)
        {
            closesocket(client);
            return false;
        }
        a = server;
        b = client;
        return true;
    }
} // namespace

std::expected<SocketPair, NetError> makeSocketPair(tui::runtime::TuiRuntime& runtime)
{
    SOCKET a = INVALID_SOCKET;
    SOCKET b = INVALID_SOCKET;
    if (!makeLoopbackPair(a, b))
        return std::unexpected(makeNetError(NetErrorCode::Other, WSAGetLastError(), "loopback pair"));

    return SocketPair {
        .first = std::unique_ptr<ISocket>(new WindowsSocket(runtime, a)),
        .second = std::unique_ptr<ISocket>(new WindowsSocket(runtime, b)),
    };
}

#endif

} // namespace endo::net::testing
