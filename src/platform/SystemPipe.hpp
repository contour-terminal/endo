// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file SystemPipe.hpp
/// @brief A cross-platform, in-process byte channel whose read end is *waitable*
///        by the event-loop reactor on every platform.
///
/// Unlike @c endo::platform::Pipe (an anonymous OS pipe for IPC), @c SystemPipe is
/// built so its read end can be multiplexed by the TUI runtime's reactor
/// (poll(2) on POSIX, WaitForMultipleObjects on Windows). Anonymous Windows pipes
/// are NOT waitable objects, so on Windows the channel is a loopback TCP socket
/// pair with the read end mapped to a waitable event via WSAEventSelect; on POSIX
/// it is a socketpair(2) whose read fd polls directly. Both expose a
/// @c waitHandle() the reactor can register and a read/write fd for the bytes.
///
/// This is what lets the same `co_await runtime.waitReadable(pipe.waitHandle())`
/// readiness test (and any cross-thread wakeup-style channel) work identically on
/// Linux, macOS, and Windows.

#include <cstddef>
#include <expected>
#include <memory>

#include <platform/PlatformError.hpp>
#include <platform/Types.hpp>

namespace endo::platform
{

/// A connected, in-process byte channel with a reactor-waitable read end.
///
/// Move-only RAII: closes all owned handles on destruction. The read and write
/// ends are connected — bytes written to @c writeFd() become readable on
/// @c readFd(), and @c waitHandle() signals when @c readFd() has data (or the
/// peer closed).
class SystemPipe
{
  public:
    virtual ~SystemPipe() = default;

    SystemPipe() = default;
    SystemPipe(SystemPipe const&) = delete;
    SystemPipe& operator=(SystemPipe const&) = delete;
    SystemPipe(SystemPipe&&) = default;
    SystemPipe& operator=(SystemPipe&&) = default;

    /// @return The native handle the reactor watches for read-readiness. On POSIX
    ///         this equals @c readFd(); on Windows it is a WSAEVENT associated with
    ///         the read socket via WSAEventSelect.
    [[nodiscard]] virtual NativeHandle waitHandle() const noexcept = 0;

    /// @return The native handle to read bytes from.
    [[nodiscard]] virtual NativeHandle readFd() const noexcept = 0;

    /// @return The native handle to write bytes to.
    [[nodiscard]] virtual NativeHandle writeFd() const noexcept = 0;

    /// Writes bytes into the channel.
    /// @param data Pointer to the bytes to send.
    /// @param size Number of bytes to send.
    /// @return Bytes written, or a @c PlatformError on failure.
    [[nodiscard]] virtual std::expected<std::size_t, PlatformError> write(void const* data,
                                                                          std::size_t size) = 0;

    /// Reads available bytes from the channel (non-blocking once @c waitHandle()
    /// has signalled readiness). On Windows this also resets the readiness event.
    /// @param data Destination buffer.
    /// @param size Maximum bytes to read.
    /// @return Bytes read (0 on peer close), or a @c PlatformError on failure.
    [[nodiscard]] virtual std::expected<std::size_t, PlatformError> read(void* data, std::size_t size) = 0;

    /// @return True if both ends and the wait handle are valid.
    [[nodiscard]] virtual bool good() const noexcept = 0;
};

/// Creates a connected @c SystemPipe.
/// @return A unique pointer to the channel on success, or a @c PlatformError.
[[nodiscard]] std::expected<std::unique_ptr<SystemPipe>, PlatformError> createSystemPipe();

} // namespace endo::platform

// Backward-compatible aliases in the endo namespace
namespace endo
{
using endo::platform::createSystemPipe;
using endo::platform::SystemPipe;
} // namespace endo
