// SPDX-License-Identifier: Apache-2.0
#include <net/windows/WindowsSocket.hpp>

#if defined(_WIN32)

    #include <utility>

namespace endo::net
{

namespace
{
    /// Maps a WSA error to a NetError category.
    [[nodiscard]] NetError fromWsa(int err, std::string context)
    {
        auto code = NetErrorCode::Other;
        switch (err)
        {
            case WSAECONNRESET: code = NetErrorCode::ConnReset; break;
            case WSAECONNREFUSED: code = NetErrorCode::ConnRefused; break;
            case WSAENOTSOCK:
            case WSAEBADF: code = NetErrorCode::BadHandle; break;
            default: break;
        }
        return makeNetError(code, err, std::move(context));
    }
} // namespace

WindowsSocket::WindowsSocket(tui::runtime::TuiRuntime& runtime,
                             SOCKET socket,
                             std::string peerAddress) noexcept:
    _runtime(runtime), _socket(socket), _event(WSACreateEvent()), _peerAddress(std::move(peerAddress))
{
    // Associate the socket's read/write/close readiness with the event so the
    // runtime can wait on it. WSAEventSelect also sets the socket non-blocking.
    if (_event != WSA_INVALID_EVENT && _socket != INVALID_SOCKET)
        WSAEventSelect(_socket, _event, FD_READ | FD_WRITE | FD_CLOSE);
}

WindowsSocket::~WindowsSocket()
{
    close();
}

void WindowsSocket::close() noexcept
{
    if (_closed)
        return;
    _closed = true;
    if (_event != WSA_INVALID_EVENT)
    {
        WSACloseEvent(_event);
        _event = WSA_INVALID_EVENT;
    }
    if (_socket != INVALID_SOCKET)
    {
        closesocket(_socket);
        _socket = INVALID_SOCKET;
    }
}

endo::coro::Task<IoResult> WindowsSocket::read(std::span<std::byte> buffer)
{
    while (true)
    {
        if (_closed || _socket == INVALID_SOCKET)
            co_return std::unexpected(makeNetError(NetErrorCode::BadHandle, 0, "read on closed socket"));

        auto const n =
            ::recv(_socket, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0);
        if (n > 0)
            co_return static_cast<std::size_t>(n);
        if (n == 0)
            co_return std::size_t { 0 }; // clean EOF

        auto const err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK)
        {
            // Reset then park on the event; WSAEventSelect re-signals while the
            // condition holds, and recv re-arms FD_READ if data remains.
            WSAResetEvent(_event);
            co_await _runtime.waitReadable(static_cast<HANDLE>(_event));
            continue;
        }
        co_return std::unexpected(fromWsa(err, "recv"));
    }
}

endo::coro::Task<IoResult> WindowsSocket::write(std::span<std::byte const> buffer)
{
    std::size_t total = 0;
    while (total < buffer.size())
    {
        if (_closed || _socket == INVALID_SOCKET)
            co_return std::unexpected(makeNetError(NetErrorCode::BadHandle, 0, "write on closed socket"));

        auto const remaining = buffer.subspan(total);
        auto const n = ::send(
            _socket, reinterpret_cast<char const*>(remaining.data()), static_cast<int>(remaining.size()), 0);
        if (n > 0)
        {
            total += static_cast<std::size_t>(n);
            continue;
        }

        auto const err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK)
        {
            WSAResetEvent(_event);
            co_await _runtime.waitWritable(static_cast<HANDLE>(_event));
            continue;
        }
        co_return std::unexpected(fromWsa(err, "send"));
    }
    co_return total;
}

} // namespace endo::net

#endif // _WIN32
