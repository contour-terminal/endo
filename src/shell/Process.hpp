// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <crispy/flags.h>

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "Error.hpp"
#include "Platform.hpp"

namespace endo
{

/// Configuration for spawning a new process.
struct SpawnConfig
{
    std::filesystem::path program;                        ///< Path to the program to execute
    std::vector<std::string> arguments;                   ///< Command line arguments (excluding program name)
    NativeHandle stdinFd = 0;                             ///< File descriptor/handle for stdin
    NativeHandle stdoutFd = 1;                            ///< File descriptor/handle for stdout
    NativeHandle stderrFd = 2;                            ///< File descriptor/handle for stderr
    std::optional<ProcessId> processGroup = std::nullopt; ///< Process group ID (0 for new group)
    bool closeExtraFds = true;                            ///< Close file descriptors > 2 after fork
    std::vector<NativeHandle> keepOpenFds;                ///< Fds to keep open even with closeExtraFds
};

/// Flags for controlling wait behavior.
///
/// These flags can be combined using the | operator.
/// Example: WaitFlag::NoHang | WaitFlag::Untraced
enum class WaitFlag
{
    None = 0,
    NoHang = 1 << 0,   ///< Return immediately if no state change (WNOHANG)
    Untraced = 1 << 1, ///< Also report stopped processes (WUNTRACED)
};

/// Type-safe flags for wait operations.
using WaitFlags = crispy::flags<WaitFlag>;

/// Abstract interface for process management operations.
///
/// This interface abstracts platform-specific process operations, enabling
/// testability and potential cross-platform support.
class ProcessManager
{
  public:
    virtual ~ProcessManager() = default;

    /// Spawns a new process with the given configuration.
    ///
    /// @param config Process spawn configuration
    /// @return The PID of the spawned process on success, or an error
    [[nodiscard]] virtual std::expected<ProcessId, ShellError> spawn(SpawnConfig const& config) = 0;

    /// Waits for a process to terminate or stop.
    ///
    /// @param pid Process ID to wait for
    /// @param flags Wait flags (default: blocking wait without stop detection)
    /// @return Wait result on success, or an error
    [[nodiscard]] virtual std::expected<WaitResult, ShellError> wait(ProcessId pid, WaitFlags flags = {}) = 0;

    /// Non-blocking wait for any process in a process group.
    ///
    /// @param pgid Process group ID (negative pid waits for any in group)
    /// @param flags Wait flags (NoHang, Untraced)
    /// @return Optional pair of (pid, result) if a process changed state, nullopt if no change
    [[nodiscard]] virtual std::expected<std::optional<std::pair<ProcessId, WaitResult>>, ShellError> waitPgid(
        ProcessId pgid, WaitFlags flags) = 0;

    /// Sends a signal to a process or process group.
    ///
    /// @param pid Process ID (positive) or process group ID (negative)
    /// @param signal Signal number to send
    /// @return Success or an error
    [[nodiscard]] virtual std::expected<void, ShellError> sendSignal(ProcessId pid, int signal) = 0;

    /// Gets the foreground process group of a terminal.
    ///
    /// @param fd File descriptor of the terminal
    /// @return Process group ID on success, or an error
    [[nodiscard]] virtual std::expected<ProcessId, ShellError> getForegroundPgrp(NativeHandle fd) = 0;

    /// Sets the foreground process group of a terminal.
    ///
    /// @param fd File descriptor of the terminal
    /// @param pgid Process group ID to set as foreground
    /// @return Success or an error
    [[nodiscard]] virtual std::expected<void, ShellError> setForegroundPgrp(NativeHandle fd,
                                                                            ProcessId pgid) = 0;

    /// Changes the current working directory.
    ///
    /// @param path New working directory path
    /// @return Success or an error
    [[nodiscard]] virtual std::expected<void, ShellError> changeDirectory(
        std::filesystem::path const& path) = 0;

    /// Opens a file with the specified flags and mode.
    ///
    /// @param path Path to the file
    /// @param flags Open flags (O_RDONLY, O_WRONLY, etc.)
    /// @param mode File mode for creation (default 0644)
    /// @return File descriptor/handle on success, or an error
    [[nodiscard]] virtual std::expected<NativeHandle, ShellError> openFile(std::filesystem::path const& path,
                                                                           int flags,
                                                                           int mode = 0644) = 0;

    /// Creates a new session (becomes session leader).
    ///
    /// @return New session ID on success, or an error
    [[nodiscard]] virtual std::expected<ProcessId, ShellError> createSession() = 0;

    /// Sets the process group for a process.
    ///
    /// @param pid Process ID (0 for current process)
    /// @param pgid Process group ID (0 for new group with pid as leader)
    /// @return Success or an error
    [[nodiscard]] virtual std::expected<void, ShellError> setProcessGroup(ProcessId pid, ProcessId pgid) = 0;

    /// Duplicates a file descriptor/handle.
    ///
    /// @param src Source file descriptor/handle
    /// @param dst Destination file descriptor/handle
    /// @return Success or an error
    [[nodiscard]] virtual std::expected<void, ShellError> duplicateFd(NativeHandle src, NativeHandle dst) = 0;

    /// Closes a file descriptor/handle.
    ///
    /// @param handle Handle to close
    virtual void closeHandle(NativeHandle handle) noexcept = 0;

    /// Closes all file descriptors/handles above stderr.
    virtual void closeExtraHandles() noexcept = 0;
};

#if !defined(_WIN32)
/// POSIX implementation of ProcessManager.
class PosixProcessManager final: public ProcessManager
{
  public:
    /// Returns the singleton instance of PosixProcessManager.
    [[nodiscard]] static PosixProcessManager& instance();

    [[nodiscard]] std::expected<ProcessId, ShellError> spawn(SpawnConfig const& config) override;
    [[nodiscard]] std::expected<WaitResult, ShellError> wait(ProcessId pid, WaitFlags flags = {}) override;
    [[nodiscard]] std::expected<std::optional<std::pair<ProcessId, WaitResult>>, ShellError> waitPgid(
        ProcessId pgid, WaitFlags flags) override;
    [[nodiscard]] std::expected<void, ShellError> sendSignal(ProcessId pid, int signal) override;
    [[nodiscard]] std::expected<ProcessId, ShellError> getForegroundPgrp(NativeHandle fd) override;
    [[nodiscard]] std::expected<void, ShellError> setForegroundPgrp(NativeHandle fd, ProcessId pgid) override;
    [[nodiscard]] std::expected<void, ShellError> changeDirectory(std::filesystem::path const& path) override;
    [[nodiscard]] std::expected<NativeHandle, ShellError> openFile(std::filesystem::path const& path,
                                                                   int flags,
                                                                   int mode = 0644) override;
    [[nodiscard]] std::expected<ProcessId, ShellError> createSession() override;
    [[nodiscard]] std::expected<void, ShellError> setProcessGroup(ProcessId pid, ProcessId pgid) override;
    [[nodiscard]] std::expected<void, ShellError> duplicateFd(NativeHandle src, NativeHandle dst) override;
    void closeHandle(NativeHandle handle) noexcept override;
    void closeExtraHandles() noexcept override;

    /// Closes all file descriptors > 2 except those in the keepOpen list.
    void closeExtraHandlesExcept(std::vector<NativeHandle> const& keepOpen) noexcept;
};
#endif // !_WIN32

} // namespace endo
