// SPDX-License-Identifier: Apache-2.0
#pragma once

#if !defined(_WIN32)

    #include <tui/runtime/TuiRuntime.hpp>

    #include <cstddef>
    #include <span>
    #include <string>

    #include <coro/Task.hpp>
    #include <net/ISocket.hpp>

namespace endo::net
{

/// A reactor-driven, non-blocking POSIX stream socket. read/write try the syscall
/// and, on EAGAIN, park the calling coroutine on the runtime's
/// waitReadable/waitWritable until the fd is ready, then retry — so a slow peer
/// suspends the coroutine rather than blocking the thread.
class PosixSocket final: public ISocket
{
  public:
    /// Wraps an already-connected, non-blocking fd.
    /// @param runtime The runtime whose reactor drives readiness (not owned).
    /// @param fd The connected socket fd (ownership transferred; closed on close()).
    /// @param peerAddress Printable peer address, or "" if unknown.
    PosixSocket(tui::runtime::TuiRuntime& runtime, int fd, std::string peerAddress = {}) noexcept;
    ~PosixSocket() override;

    PosixSocket(PosixSocket const&) = delete;
    PosixSocket& operator=(PosixSocket const&) = delete;
    PosixSocket(PosixSocket&&) = delete;
    PosixSocket& operator=(PosixSocket&&) = delete;

    [[nodiscard]] endo::coro::Task<IoResult> read(std::span<std::byte> buffer) override;
    [[nodiscard]] endo::coro::Task<IoResult> write(std::span<std::byte const> buffer) override;

    [[nodiscard]] std::string peerAddress() const override { return _peerAddress; }

    void close() noexcept override;

    [[nodiscard]] bool isClosed() const noexcept override { return _closed; }

    /// @return The underlying fd (for diagnostics/tests).
    [[nodiscard]] int native() const noexcept { return _fd; }

  private:
    tui::runtime::TuiRuntime& _runtime;
    int _fd;
    std::string _peerAddress;
    bool _closed = false;
};

} // namespace endo::net

#endif // !_WIN32
