// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tui/Error.hpp>
#include <tui/InputEvent.hpp>
#include <tui/TerminalInput.hpp>
#include <tui/TerminalOutput.hpp>

#include <cstdint>
#include <functional>
#include <vector>

namespace tui
{

/// @brief Detected terminal color scheme (dark or light mode).
enum class ColorScheme : std::uint8_t
{
    Unknown, ///< Color scheme not yet detected.
    Dark,    ///< Dark background.
    Light,   ///< Light background.
};

/// @brief Top-level terminal coordinator that owns both input and output subsystems.
///
/// Manages initialization order, cleanup, and SIGWINCH handler installation.
/// Provides convenience methods that delegate to TerminalInput and TerminalOutput.
class Terminal
{
  public:
    Terminal();
    ~Terminal();

    Terminal(Terminal const&) = delete;
    auto operator=(Terminal const&) -> Terminal& = delete;
    Terminal(Terminal&&) = delete;
    auto operator=(Terminal&&) -> Terminal& = delete;

    /// @brief Initializes both input and output subsystems and installs the SIGWINCH handler.
    /// @return Success or an error.
    [[nodiscard]] auto initialize() -> VoidResult;

    /// @brief Shuts down both subsystems and restores the original SIGWINCH handler.
    void shutdown();

    /// @brief Returns a reference to the input subsystem.
    [[nodiscard]] auto input() noexcept -> TerminalInput&;

    /// @brief Returns a reference to the output subsystem.
    [[nodiscard]] auto output() noexcept -> TerminalOutput&;

    /// @brief Convenience: polls for input events with the given timeout.
    /// @param timeoutMs -1 = block, 0 = non-blocking, >0 = timeout in ms.
    /// @return Vector of parsed events.
    [[nodiscard]] auto poll(int timeoutMs = -1) -> std::vector<InputEvent>;

    /// @brief Returns terminal width in columns.
    [[nodiscard]] auto columns() const noexcept -> int;

    /// @brief Returns terminal height in rows.
    [[nodiscard]] auto rows() const noexcept -> int;

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

    /// @brief Queries the current cursor position from the terminal.
    /// @return Pair of (row, column), both 1-based, or (0, 0) on failure.
    [[nodiscard]] auto queryCursorPosition() -> std::pair<int, int>;

    /// @brief Returns the cached color scheme (dark or light mode).
    [[nodiscard]] auto colorScheme() const noexcept -> ColorScheme;

    /// @brief Registers a callback for color scheme change notifications.
    ///
    /// The callback is invoked when the terminal reports a color scheme change
    /// via CSI ? 997 ; N n (triggered by DEC mode 2031 subscription).
    /// Multiple handlers can be registered.
    /// @param callback The callback to invoke on scheme change.
    void onColorSchemeChanged(std::function<void(ColorScheme)> callback);

    /// @brief Called internally by VtParser when a color scheme report is received.
    /// @param scheme The reported color scheme.
    void handleColorSchemeReport(ColorScheme scheme);

  private:
    TerminalInput _input;
    TerminalOutput _output;
    bool _initialized = false;
    ColorScheme _colorScheme = ColorScheme::Unknown;
    std::vector<std::function<void(ColorScheme)>> _colorSchemeCallbacks;
};

} // namespace tui
