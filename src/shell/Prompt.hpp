// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <optional>
#include <string>

#include <tui/Screen.hpp>
#include <tui/Terminal.hpp>

namespace endo
{

// Forward declarations
class Completer;
class PromptComponent;

/// @brief TUI-based prompt using tui::Terminal and tui::InputField.
///
/// Provides a rich editing experience with selection, undo/redo, clipboard,
/// and history support via the TUI library. Supports multiline editing with
/// an auto-growing editor region that expands up to 50% of terminal height.
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

    /// @brief Displays the prompt without waiting for input.
    ///
    /// Call this before blocking on poll() to ensure the prompt is visible.
    /// After input is available, call read() or processInput() to handle it.
    void display();

    /// @brief Enables or disables multiline editing mode.
    /// @param enable True to enable multiline editing.
    void setMultilineEnabled(bool enable);

    /// @brief Returns whether multiline editing is enabled.
    [[nodiscard]] bool isMultilineEnabled() const noexcept;

    /// @brief Suspends terminal modes for external command execution.
    ///
    /// Disables raw mode and terminal protocols (CSI u, mouse, bracketed paste)
    /// so that external programs see a normal terminal state.
    /// Call resume() after the command completes.
    void suspend();

    /// @brief Resumes terminal modes after external command execution.
    ///
    /// Re-enables raw mode and terminal protocols.
    void resume();

    /// @brief Sets the completer to use for autocompletion.
    /// @param completer The completer (ownership not transferred).
    void setCompleter(Completer* completer);

    /// @brief RAII helper for suspend/resume scoping.
    ///
    /// Automatically calls suspend() on construction and resume() on destruction,
    /// ensuring terminal modes are properly restored even if exceptions occur.
    class [[nodiscard]] ScopedSuspend
    {
      public:
        explicit ScopedSuspend(Prompt& prompt) noexcept: _prompt(prompt) { _prompt.suspend(); }

        ~ScopedSuspend() { _prompt.resume(); }

        ScopedSuspend(ScopedSuspend const&) = delete;
        ScopedSuspend& operator=(ScopedSuspend const&) = delete;
        ScopedSuspend(ScopedSuspend&&) = delete;
        ScopedSuspend& operator=(ScopedSuspend&&) = delete;

      private:
        Prompt& _prompt;
    };

  private:
    tui::Terminal _terminal;
    std::unique_ptr<tui::Screen> _screen;
    std::unique_ptr<PromptComponent> _promptComponent;
    Completer* _completer = nullptr;
    std::string _promptStr = "> ";
    bool _initialized = false;
    bool _aborted = false;
    bool _multilineEnabled = true; ///< Enable multiline editing by default

    void initialize();
};

} // namespace endo
