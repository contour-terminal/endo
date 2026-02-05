// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>

#include <tui/InputField.hpp>
#include <tui/Terminal.hpp>

namespace endo
{

/// @brief TUI-based prompt using tui::Terminal and tui::InputField.
///
/// Provides a rich editing experience with selection, undo/redo, clipboard,
/// and history support via the TUI library.
class Prompt
{
  public:
    Prompt();
    ~Prompt();

    /// @brief Returns whether the prompt is ready to accept input.
    [[nodiscard]] bool ready() const;

    /// @brief Reads a line of input from the user.
    ///
    /// Blocks until the user submits (Enter) or aborts (Ctrl+C/Ctrl+D).
    /// @return The input line, or empty string on EOF/abort.
    [[nodiscard]] std::string read();

    /// @brief Sets the prompt string displayed before user input.
    /// @param promptStr The prompt string.
    void setPrompt(std::string_view promptStr);

    /// @brief Adds an entry to the command history.
    /// @param entry The command to add.
    void addHistory(std::string entry);

    /// @brief Returns the input file descriptor for poll() integration.
    [[nodiscard]] int inputFd() const noexcept;

    /// @brief Processes pending input events without blocking.
    ///
    /// Call this when poll() indicates input is available.
    /// @return The completed input line if user submitted, nullopt otherwise.
    [[nodiscard]] std::optional<std::string> processInput();

    /// @brief Handles terminal resize events.
    void onResize();

  private:
    tui::Terminal _terminal;
    tui::InputField _inputField;
    std::string _promptStr = "> ";
    bool _initialized = false;
    bool _aborted = false;

    void initialize();
    void render();
};

} // namespace endo
