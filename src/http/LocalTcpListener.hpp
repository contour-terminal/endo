// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <expected>
#include <string>
#include <string_view>

namespace endo::http
{

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
    /// @return The accepted client socket fd on success, or an error message.
    [[nodiscard]] auto acceptConnection(std::chrono::seconds timeout) -> std::expected<int, std::string>;

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
    int _listenFd = -1;
};

/// Cross-platform socket close helper.
/// @param fd The socket file descriptor to close. No-op if fd < 0.
void closeSocket(int fd);

} // namespace endo::http
