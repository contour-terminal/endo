// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include <termios.h>

#include <tui/Error.hpp>
#include <tui/InputEvent.hpp>
#include <tui/VtParser.hpp>

namespace tui
{

/// @brief Handles raw terminal input, polling, and event production.
///
/// Manages raw mode, enables Kitty keyboard protocol, SGR mouse reporting,
/// and bracketed paste. Uses poll() for non-blocking reads with timeout.
/// SIGWINCH is handled via a self-pipe pattern for thread-safe resize notification.
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
    /// Writes to the self-pipe to wake poll(). Typically called from a SIGWINCH handler.
    /// @param cols New column count.
    /// @param rows New row count.
    void notifyResize(int cols, int rows);

    /// @brief Returns the file descriptor for the resize notification pipe (read end).
    [[nodiscard]] auto resizePipeReadFd() const noexcept -> int;

  private:
    VtParser _parser;
    int _fd = 0; // STDIN_FILENO
    struct termios _origTermios {};
    bool _rawMode = false;
    int _resizePipe[2] = { -1, -1 }; ///< Self-pipe for SIGWINCH.

    void enableRawMode();
    void disableRawMode();
    void enableProtocols();
    void disableProtocols();
};

} // namespace tui
