// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <crispy/flags.h>

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <platform/PlatformError.hpp>
#include <platform/Types.hpp>
#include <platform/WaitResult.hpp>

namespace endo::platform
{

/// Configuration for spawning a new process.
struct SpawnConfig
{
    std::filesystem::path program;         ///< Path to the program to execute
    std::vector<std::string> arguments;    ///< Command line arguments (excluding program name)
    NativeHandle stdinFd = InvalidHandle;  ///< File descriptor/handle for stdin (InvalidHandle = standard)
    NativeHandle stdoutFd = InvalidHandle; ///< File descriptor/handle for stdout (InvalidHandle = standard)
    NativeHandle stderrFd = InvalidHandle; ///< File descriptor/handle for stderr (InvalidHandle = standard)
    /// POSIX process group ID (0 for a new group), driving setpgid. On Windows this only
    /// feeds process-group bookkeeping for waitPgid; creating a new *console* process group
    /// (CREATE_NEW_PROCESS_GROUP) is controlled separately by @ref newConsoleProcessGroup.
    std::optional<ProcessId> processGroup = std::nullopt;
    bool closeExtraFds = true;             ///< Close file descriptors > 2 after fork
    std::vector<NativeHandle> keepOpenFds; ///< Fds to keep open even with closeExtraFds

    /// On Windows, create the child in a new console process group
    /// (CREATE_NEW_PROCESS_GROUP), shielding it from the console's Ctrl+C. Set this
    /// for background/detached jobs. Foreground jobs must leave this false so they
    /// share the shell's console group and receive the console's Ctrl+C. Ignored on
    /// POSIX, where process-group placement is driven by @ref processGroup.
    bool newConsoleProcessGroup = false;

    /// Configures this spawn as a background/detached job: places the child in a new
    /// process group and, on Windows, a new console process group so it is shielded
    /// from the console's Ctrl+C. Keeps @ref processGroup and @ref
    /// newConsoleProcessGroup in lockstep so callers express background intent once.
    void markBackgroundGroup() noexcept
    {
        processGroup = 0;
        newConsoleProcessGroup = true;
    }
};

/// Flags for controlling wait behavior.
///
/// These flags can be combined using the | operator.
/// Example: WaitFlag::NoHang | WaitFlag::Untraced
enum class WaitFlag // NOLINT(performance-enum-size)
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
    [[nodiscard]] virtual std::expected<ProcessId, PlatformError> spawn(SpawnConfig const& config) = 0;

    /// Waits for a process to terminate or stop.
    ///
    /// @param pid Process ID to wait for
    /// @param flags Wait flags (default: blocking wait without stop detection)
    /// @return Wait result on success, or an error
    [[nodiscard]] virtual std::expected<WaitResult, PlatformError> wait(ProcessId pid,
                                                                        WaitFlags flags = {}) = 0;

    /// Non-blocking wait for any process in a process group.
    ///
    /// @param pgid Process group ID (negative pid waits for any in group)
    /// @param flags Wait flags (NoHang, Untraced)
    /// @return Optional pair of (pid, result) if a process changed state, nullopt if no change
    [[nodiscard]] virtual std::expected<std::optional<std::pair<ProcessId, WaitResult>>, PlatformError>
    waitPgid(ProcessId pgid, WaitFlags flags) = 0;

    /// Sends a signal to a process or process group.
    ///
    /// @param pid Process ID (positive) or process group ID (negative)
    /// @param signal Signal number to send
    /// @return Success or an error
    [[nodiscard]] virtual std::expected<void, PlatformError> sendSignal(ProcessId pid, int signal) = 0;

    /// Gets the foreground process group of a terminal.
    ///
    /// @param fd File descriptor of the terminal
    /// @return Process group ID on success, or an error
    [[nodiscard]] virtual std::expected<ProcessId, PlatformError> getForegroundPgrp(NativeHandle fd) = 0;

    /// Sets the foreground process group of a terminal.
    ///
    /// @param fd File descriptor of the terminal
    /// @param pgid Process group ID to set as foreground
    /// @return Success or an error
    [[nodiscard]] virtual std::expected<void, PlatformError> setForegroundPgrp(NativeHandle fd,
                                                                               ProcessId pgid) = 0;

    /// Opens a file with the specified flags and mode.
    ///
    /// @param path Path to the file
    /// @param flags Open flags (O_RDONLY, O_WRONLY, etc.)
    /// @param mode File mode for creation (default 0644)
    /// @return File descriptor/handle on success, or an error
    [[nodiscard]] virtual std::expected<NativeHandle, PlatformError> openFile(
        std::filesystem::path const& path, int flags, int mode = 0644) = 0;

    /// Creates a new session (becomes session leader).
    ///
    /// @return New session ID on success, or an error
    [[nodiscard]] virtual std::expected<ProcessId, PlatformError> createSession() = 0;

    /// Sets the process group for a process.
    ///
    /// @param pid Process ID (0 for current process)
    /// @param pgid Process group ID (0 for new group with pid as leader)
    /// @return Success or an error
    [[nodiscard]] virtual std::expected<void, PlatformError> setProcessGroup(ProcessId pid,
                                                                             ProcessId pgid) = 0;

    /// Duplicates a file descriptor/handle.
    ///
    /// @param src Source file descriptor/handle
    /// @param dst Destination file descriptor/handle
    /// @return Success or an error
    [[nodiscard]] virtual std::expected<void, PlatformError> duplicateFd(NativeHandle src,
                                                                         NativeHandle dst) = 0;

    /// Closes a file descriptor/handle.
    ///
    /// @param handle Handle to close
    virtual void closeHandle(NativeHandle handle) noexcept = 0;

    /// Closes all file descriptors/handles above stderr.
    virtual void closeExtraHandles() noexcept = 0;
};

#ifdef _WIN32

/// Windows implementation of ProcessManager.
///
/// Uses CreateProcess for spawning, WaitForSingleObject for waiting,
/// and tracks process handles internally for lifecycle management.
/// Defined in windows/WindowsProcess.cpp.
class WindowsProcessManager final: public ProcessManager
{
  public:
    /// Returns the singleton instance of WindowsProcessManager.
    [[nodiscard]] static WindowsProcessManager& instance();

    [[nodiscard]] std::expected<ProcessId, PlatformError> spawn(SpawnConfig const& config) override;
    [[nodiscard]] std::expected<WaitResult, PlatformError> wait(ProcessId pid, WaitFlags flags = {}) override;
    [[nodiscard]] std::expected<std::optional<std::pair<ProcessId, WaitResult>>, PlatformError> waitPgid(
        ProcessId pgid, WaitFlags flags) override;
    [[nodiscard]] std::expected<void, PlatformError> sendSignal(ProcessId pid, int signal) override;
    [[nodiscard]] std::expected<ProcessId, PlatformError> getForegroundPgrp(NativeHandle fd) override;
    [[nodiscard]] std::expected<void, PlatformError> setForegroundPgrp(NativeHandle fd,
                                                                       ProcessId pgid) override;
    [[nodiscard]] std::expected<NativeHandle, PlatformError> openFile(std::filesystem::path const& path,
                                                                      int flags,
                                                                      int mode = 0644) override;
    [[nodiscard]] std::expected<ProcessId, PlatformError> createSession() override;
    [[nodiscard]] std::expected<void, PlatformError> setProcessGroup(ProcessId pid, ProcessId pgid) override;
    [[nodiscard]] std::expected<void, PlatformError> duplicateFd(NativeHandle src, NativeHandle dst) override;
    void closeHandle(NativeHandle handle) noexcept override;
    void closeExtraHandles() noexcept override;

  private:
    std::unordered_map<ProcessId, HANDLE> _processHandles;               ///< PID -> process HANDLE
    std::unordered_map<ProcessId, std::vector<ProcessId>> _groupMembers; ///< group -> PIDs

    /// @brief Terminates a process by its tracked handle.
    ///
    /// Used as the delivery mechanism for SIGTERM/SIGKILL, and as the fallback for SIGINT
    /// when the target is not its own console process group (so CTRL_C cannot be delivered
    /// to it individually).
    ///
    /// @param pid Process ID whose handle should be terminated.
    /// @return Success, or PlatformError::SignalFailed if the handle is unknown or termination fails.
    [[nodiscard]] auto terminateByHandle(ProcessId pid) -> std::expected<void, PlatformError>;
    /// @brief Suspends all threads of a process using Toolhelp32.
    [[nodiscard]] auto suspendProcess(ProcessId pid) -> std::expected<void, PlatformError>;
    /// @brief Resumes all threads of a process using Toolhelp32.
    [[nodiscard]] auto resumeProcess(ProcessId pid) -> std::expected<void, PlatformError>;
};
#else
/// POSIX implementation of ProcessManager.
class PosixProcessManager final: public ProcessManager
{
  public:
    /// Returns the singleton instance of PosixProcessManager.
    [[nodiscard]] static PosixProcessManager& instance();

    [[nodiscard]] std::expected<ProcessId, PlatformError> spawn(SpawnConfig const& config) override;
    [[nodiscard]] std::expected<WaitResult, PlatformError> wait(ProcessId pid, WaitFlags flags = {}) override;
    [[nodiscard]] std::expected<std::optional<std::pair<ProcessId, WaitResult>>, PlatformError> waitPgid(
        ProcessId pgid, WaitFlags flags) override;
    [[nodiscard]] std::expected<void, PlatformError> sendSignal(ProcessId pid, int signal) override;
    [[nodiscard]] std::expected<ProcessId, PlatformError> getForegroundPgrp(NativeHandle fd) override;
    [[nodiscard]] std::expected<void, PlatformError> setForegroundPgrp(NativeHandle fd,
                                                                       ProcessId pgid) override;
    [[nodiscard]] std::expected<NativeHandle, PlatformError> openFile(std::filesystem::path const& path,
                                                                      int flags,
                                                                      int mode = 0644) override;
    [[nodiscard]] std::expected<ProcessId, PlatformError> createSession() override;
    [[nodiscard]] std::expected<void, PlatformError> setProcessGroup(ProcessId pid, ProcessId pgid) override;
    [[nodiscard]] std::expected<void, PlatformError> duplicateFd(NativeHandle src, NativeHandle dst) override;
    void closeHandle(NativeHandle handle) noexcept override;
    void closeExtraHandles() noexcept override;

    /// Closes all file descriptors > 2 except those in the keepOpen list.
    static void closeExtraHandlesExcept(std::vector<NativeHandle> const& keepOpen) noexcept;
};
#endif

} // namespace endo::platform

// Backward-compatible aliases in the endo namespace
namespace endo
{
#if defined(_WIN32)
using endo::platform::WindowsProcessManager;
#else
using endo::platform::PosixProcessManager;
#endif
using endo::platform::ProcessManager;
using endo::platform::SpawnConfig;
using endo::platform::WaitFlag;
using endo::platform::WaitFlags;
} // namespace endo
