// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tui/Error.hpp>
#include <tui/InputEvent.hpp>
#include <tui/TerminalInput.hpp>
#include <tui/TerminalOutput.hpp>

#include <cstdint>
#include <functional>
#include <memory>
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
    /// @brief Constructs a Terminal with the default real TerminalOutput.
    Terminal();

    /// @brief Constructs a Terminal with a custom TerminalOutput (for dependency injection in tests).
    /// @param output The output implementation to use (ownership transferred).
    explicit Terminal(std::unique_ptr<TerminalOutput> output);

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

    /// @brief Queries the cell size in pixels from the terminal via CSI 16 t.
    /// @return Pair of (width, height) in pixels, or (0, 0) on failure/timeout.
    [[nodiscard]] auto queryCellSize() -> std::pair<int, int>;

    /// @brief Returns the cached cell pixel width (0 if unknown).
    [[nodiscard]] auto cellPixelWidth() const noexcept -> int;

    /// @brief Returns the cached cell pixel height (0 if unknown).
    [[nodiscard]] auto cellPixelHeight() const noexcept -> int;

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

    /// @brief Consumes a focus-change report, updating state and notifying handlers.
    ///
    /// Public so the coroutine runtime's event source can route the report the
    /// same way poll() does (it is otherwise an internal protocol response).
    /// @param focused True if the terminal gained focus, false if it lost focus.
    /// @return True if the focus state actually changed (handlers were notified).
    bool handleFocusEvent(bool focused);

    /// @brief Removes protocol-response events from @p events, dispatching the
    /// color-scheme and focus reports to their handlers and dropping the rest.
    ///
    /// Shared by poll() and the coroutine runtime's event source so the
    /// "what is an internal report vs. application input" policy lives in one
    /// place (the @c tui::isProtocolReport predicate). @c CursorPositionReport /
    /// @c CellSizeReport are dropped (their values are consumed synchronously by
    /// the query methods).
    /// @param events The decoded events to filter in place.
    /// @return True if a focus-change report was dispatched (so a caller waiting
    ///         on activity can redraw); false otherwise.
    bool consumeProtocolReports(std::vector<InputEvent>& events);

    /// @brief Returns whether the terminal window currently has focus.
    [[nodiscard]] auto isFocused() const noexcept -> bool;

    /// @brief Returns whether the terminal supports HUD overlay mode (DEC mode 2035).
    ///
    /// When supported, the command palette can render on a transparent HUD layer
    /// above the primary screen content. When not supported, falls back to inline rendering.
    [[nodiscard]] auto hudSupported() const noexcept -> bool;

    /// @brief Registers a callback for focus change notifications (DECSET 1004).
    ///
    /// The callback is invoked when the terminal reports focus gained or lost.
    /// Multiple handlers can be registered.
    /// @param callback The callback to invoke on focus change (true = focused).
    void onFocusChanged(std::function<void(bool)> callback);

  private:
    TerminalInput _input;
    std::unique_ptr<TerminalOutput> _output;
    bool _initialized = false;
    bool _mockMode = false; ///< True when using a mock output (skip input/signal init).
    ColorScheme _colorScheme = ColorScheme::Unknown;
    std::vector<std::function<void(ColorScheme)>> _colorSchemeCallbacks;
    bool _focused = true;       ///< Whether the terminal window has focus (assume focused on startup).
    bool _hudSupported = false; ///< Whether terminal supports HUD overlay (DEC mode 2035).
    std::vector<std::function<void(bool)>> _focusCallbacks;
    int _cellPixelWidth = 0;  ///< Cached cell width in pixels (0 if unknown).
    int _cellPixelHeight = 0; ///< Cached cell height in pixels (0 if unknown).

    /// @brief Queries a DEC private mode via DECRQM and waits for the response.
    /// @param mode The DEC private mode number to query.
    /// @return True if the mode is recognized (status 1 or 2), false otherwise.
    [[nodiscard]] auto queryDecMode(int mode) -> bool;
};

} // namespace tui
