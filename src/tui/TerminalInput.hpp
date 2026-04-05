// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tui/Error.hpp>
#include <tui/InputEvent.hpp>
#include <tui/VtParser.hpp>

#include <string_view>
#include <vector>

#include <platform/Types.hpp>

#if defined(_WIN32)
    #include <windows.h>
#else
    #include <termios.h>
#endif

namespace endo::platform
{
class Wakeup;
} // namespace endo::platform

namespace tui
{

/// @brief Handles raw terminal input, polling, and event production.
///
/// Manages raw mode, enables Kitty keyboard protocol, SGR mouse reporting,
/// and bracketed paste. Uses poll() for non-blocking reads with timeout.
/// On POSIX, SIGWINCH is handled via a self-pipe pattern for thread-safe resize notification.
/// On Windows, WINDOW_BUFFER_SIZE_EVENT is used for resize detection.
class TerminalInput
{
  public:
    TerminalInput();
    ~TerminalInput();

    TerminalInput(TerminalInput const&) = delete;
    auto operator=(TerminalInput const&) -> TerminalInput& = delete;
    TerminalInput(TerminalInput&&) = delete;
    auto operator=(TerminalInput&&) -> TerminalInput& = delete;

    /// @brief Initializes raw mode and enables terminal protocols.
    /// @return Success or an IoError on failure.
    [[nodiscard]] auto initialize() -> VoidResult;

    /// @brief Restores the original terminal state.
    void shutdown();

    /// @brief Polls for input events with optional timeout.
    /// @param timeoutMs -1 = block indefinitely, 0 = non-blocking, >0 = timeout in milliseconds.
    /// @return Vector of parsed events (empty on timeout or no data).
    [[nodiscard]] auto poll(int timeoutMs = -1) -> std::vector<InputEvent>;

    /// @brief Injects a synthetic resize event.
    ///
    /// On POSIX, writes to the self-pipe to wake poll().
    /// On Windows, signals a manual-reset event.
    /// @param cols New column count.
    /// @param rows New row count.
    void notifyResize(int cols, int rows);

    /// @brief Returns the file descriptor for the resize notification pipe (read end).
    /// On Windows, returns -1 (resize is event-based).
    [[nodiscard]] auto resizePipeReadFd() const noexcept -> int;

    /// @brief Suspends terminal protocols and raw mode for external command execution.
    ///
    /// Call this before executing external commands to restore the terminal to
    /// a normal state that programs expect. Call resume() after the command completes.
    void suspend();

    /// @brief Resumes terminal protocols and raw mode after external command execution.
    ///
    /// Call this after an external command completes to restore the shell's
    /// terminal configuration.
    void resume();

    /// @brief Returns whether the terminal is currently suspended.
    [[nodiscard]] auto isSuspended() const noexcept -> bool;

    /// @brief Enables or disables any-motion mouse tracking (mode 1003).
    ///
    /// When enabled, the terminal reports all mouse movements (not just button presses),
    /// which is required for hover tooltip support. This should only be enabled after
    /// confirming passive mouse tracking (mode 2029) support via DECRQPM, to avoid
    /// capturing the mouse in terminals that don't support passive tracking.
    /// @param enabled True to enable, false to disable.
    void setAnyMotionTracking(bool enabled);

    /// @brief Sets a cross-platform wakeup handle for poll() integration.
    ///
    /// When set, poll() will also monitor the wakeup's native handle alongside
    /// stdin and the resize pipe/event. This allows a background thread to wake
    /// the poll() call by signaling the wakeup.
    /// @param wakeup Pointer to the wakeup primitive (must outlive this object), or nullptr to disable.
    void setWakeup(endo::platform::Wakeup* wakeup);

  private:
    VtParser _parser;
    bool _rawMode = false;
    bool _suspended = false;         ///< True when suspended for external command execution.
    bool _anyMotionTracking = false; ///< True when any-motion tracking (mode 1003) should be enabled.

#if defined(_WIN32)
    HANDLE _hStdin = INVALID_HANDLE_VALUE;
    HANDLE _hStdout = INVALID_HANDLE_VALUE;
    DWORD _originalInputMode = 0;
    DWORD _originalOutputMode = 0;
    HANDLE _resizeEvent = nullptr; ///< Manual-reset event for resize notification.
#else
    int _fd = 0;    // STDIN_FILENO
    int _outFd = 1; // STDOUT_FILENO
    struct termios _origTermios {};
    int _resizePipe[2] = { -1, -1 }; ///< Self-pipe for SIGWINCH.
#endif

    endo::platform::Wakeup* _wakeup = nullptr; ///< Optional cross-thread wakeup handle.

    void enableRawMode();
    void disableRawMode();
    void enableProtocols();
    void disableProtocols();
    void writeProtocol(std::string_view data) const;
};

} // namespace tui
