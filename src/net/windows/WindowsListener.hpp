// SPDX-License-Identifier: Apache-2.0
#pragma once

#if defined(_WIN32)

    #include <net/IListener.hpp>

// clang-format off
    #include <winsock2.h>
    #include <windows.h>
// clang-format on

    #include <tui/runtime/TuiRuntime.hpp>

    #include <cstdint>
    #include <memory>
    #include <string_view>

    #include <coro/Task.hpp>

namespace endo::net
{

/// A reactor-driven Windows TCP listener. Accept readiness is observed through a
/// WSAEVENT associated with the listening socket via WSAEventSelect(FD_ACCEPT);
/// accept() parks on the event, then accepts the pending connection as a
/// WindowsSocket.
class WindowsListener final: public IListener
{
  public:
    ~WindowsListener() override;

    WindowsListener(WindowsListener const&) = delete;
    WindowsListener& operator=(WindowsListener const&) = delete;
    WindowsListener(WindowsListener&&) = delete;
    WindowsListener& operator=(WindowsListener&&) = delete;

    /// Binds and listens on @p host : @p port.
    /// @param runtime The runtime whose reactor drives accept readiness (not owned).
    /// @param host The bind address.
    /// @param port The bind port; 0 requests an ephemeral port.
    /// @param backlog The listen backlog.
    /// @return The bound listener, or a @c NetError on failure.
    [[nodiscard]] static std::expected<std::unique_ptr<WindowsListener>, NetError> bind(
        tui::runtime::TuiRuntime& runtime, std::string_view host, std::uint16_t port, int backlog = 128);

    [[nodiscard]] endo::coro::Task<AcceptResult> accept() override;

    [[nodiscard]] std::uint16_t localPort() const noexcept override { return _localPort; }

    void close() noexcept override;

  private:
    WindowsListener(tui::runtime::TuiRuntime& runtime,
                    SOCKET socket,
                    WSAEVENT event,
                    std::uint16_t localPort) noexcept;

    tui::runtime::TuiRuntime& _runtime;
    SOCKET _socket;
    WSAEVENT _event;
    std::uint16_t _localPort;
    bool _closed = false;
};

} // namespace endo::net

#endif // _WIN32
