// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>

namespace endo::http
{

#if defined(_WIN32)
/// A socket as the platform's socket API names it: Winsock's `SOCKET` (a `UINT_PTR`), spelled
/// without `<winsock2.h>` so that includers do not get the Windows API with this header.
using SocketHandle = std::uintptr_t;

/// The value Winsock's `socket()` and `accept()` return on failure (`INVALID_SOCKET`).
inline constexpr SocketHandle InvalidSocket = ~SocketHandle { 0 };
#else
/// A socket as the platform's socket API names it: a POSIX file descriptor.
using SocketHandle = int;

/// The value POSIX `socket()` and `accept()` return on failure.
inline constexpr SocketHandle InvalidSocket = -1;
#endif

/// RAII wrapper for a localhost TCP listening socket with ephemeral port assignment.
///
/// Provides the common socket lifecycle (bind, listen, accept) shared by
/// OAuthCallbackServer and test HTTP servers.
class LocalTcpListener
{
  public:
    LocalTcpListener() = default;
    ~LocalTcpListener();

    LocalTcpListener(LocalTcpListener const&) = delete;
    LocalTcpListener& operator=(LocalTcpListener const&) = delete;
    LocalTcpListener(LocalTcpListener&&) noexcept;
    LocalTcpListener& operator=(LocalTcpListener&&) noexcept;

    /// Binds to 127.0.0.1 with an OS-assigned ephemeral port and starts listening.
    /// @return The assigned port number on success, or an error message.
    [[nodiscard]] auto start() -> std::expected<uint16_t, std::string>;

    /// Blocks until an incoming connection arrives or the timeout expires.
    /// @param timeout Maximum time to wait for a connection.
    /// @return The accepted client socket on success, or an error message.
    [[nodiscard]] auto acceptConnection(std::chrono::seconds timeout)
        -> std::expected<SocketHandle, std::string>;

    /// Accepts one connection, drains the HTTP request, sends @p response, and closes the client socket.
    /// Convenience for test servers that serve a single canned HTTP response.
    /// @param timeout  Maximum time to wait for a connection.
    /// @param response The full HTTP response (status line + headers + body) to send.
    /// @return void on success, or an error message.
    [[nodiscard]] auto serveOnce(std::chrono::seconds timeout, std::string_view response)
        -> std::expected<void, std::string>;

    /// Closes the listening socket if still open.
    void close();

  private:
    SocketHandle _listenSocket = InvalidSocket;
};

/// Cross-platform socket close helper.
/// @param handle The socket to close. No-op if it is InvalidSocket.
void closeSocket(SocketHandle handle);

/// Cross-platform socket receive helper.
/// @param handle The connected socket to read from.
/// @param buffer The bytes to fill; at most `buffer.size()` are received.
/// @return The number of bytes received, 0 when the peer closed the connection, or a negative value on error.
[[nodiscard]] auto receiveFromSocket(SocketHandle handle, std::span<char> buffer) -> std::ptrdiff_t;

/// Cross-platform socket send helper.
/// @param handle The connected socket to write to.
/// @param data The bytes to send.
/// @return The number of bytes sent, or a negative value on error.
auto sendToSocket(SocketHandle handle, std::string_view data) -> std::ptrdiff_t;

} // namespace endo::http
