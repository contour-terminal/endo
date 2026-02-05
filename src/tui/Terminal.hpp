// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include <tui/Error.hpp>
#include <tui/InputEvent.hpp>
#include <tui/TerminalInput.hpp>
#include <tui/TerminalOutput.hpp>

namespace tui
{

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

  private:
    TerminalInput _input;
    TerminalOutput _output;
    bool _initialized = false;
};

} // namespace tui
