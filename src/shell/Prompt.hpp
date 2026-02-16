// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/PromptConfig.hpp>
#include <shell/PromptModule.hpp>

#include "Platform.hpp"

#include <tui/KeyBindings.hpp>
#include <tui/Screen.hpp>
#include <tui/Terminal.hpp>

#include <memory>
#include <optional>
#include <set>
#include <string>

namespace endo
{

// Forward declarations
class Completer;
class CommandResolver;
class History;
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

    /// @brief Sets the prompt configuration (layout, modules, etc.).
    /// @param config The new prompt configuration.
    void setPromptConfig(PromptConfig config);

    /// @brief Returns the current prompt configuration.
    [[nodiscard]] PromptConfig const& promptConfig() const noexcept;

    /// @brief Sets the prompt context (CWD, exit code, etc.) for module evaluation.
    /// @param context The new prompt context.
    void setPromptContext(PromptContext context);

    /// @brief Returns the Terminal for color scheme access.
    [[nodiscard]] tui::Terminal& terminal() noexcept { return _terminal; }

    /// @brief Sets externally known F# names for diagnostics suppression.
    ///
    /// @param names The set of known F# names (persisted from prior REPL prompts).
    void setKnownFSharpNames(std::set<std::string> names);

    /// @brief Sets the completer to use for autocompletion.
    /// @param completer The completer (ownership not transferred).
    void setCompleter(Completer* completer);

    /// @brief Sets the history source for inline history cycling.
    /// @param history The history object (ownership not transferred).
    void setHistory(History const* history);

    // ========================================================================
    // Keybindings
    // ========================================================================

    /// @brief Binds a key chord to an edit action.
    /// @param chord The key chord to bind.
    /// @param action The action to execute when the chord is pressed.
    void bindKey(tui::KeyChord chord, tui::EditAction action);

    /// @brief Removes a keybinding.
    /// @param chord The key chord to unbind.
    void unbindKey(tui::KeyChord chord);

    /// @brief Resets all keybindings to defaults.
    void resetKeyBindings();

    /// @brief Returns the current keybindings (const).
    [[nodiscard]] tui::KeyBindings const& keyBindings() const;

    /// @brief Returns the current keybindings (mutable).
    [[nodiscard]] tui::KeyBindings& keyBindings();

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
    std::unique_ptr<CommandResolver> _commandResolver;
    Completer* _completer = nullptr;
    History const* _history = nullptr;
    std::string _promptStr = "> ";
    PromptConfig _promptConfig;
    bool _initialized = false;
    bool _aborted = false;
    bool _multilineEnabled = true; ///< Enable multiline editing by default

    void initialize();
    void setupHoverCallbacks();

    /// @brief Replaces the full prompt with a compact transient indicator before scrolling off.
    /// @param inputText The user's submitted command text.
    void emitTransientPrompt(std::string_view inputText);
};

/// @brief Emits a dim partial-line indicator (⏎) and moves to a fresh line.
///
/// Called when a command's output did not end with a newline, to visually mark the
/// incomplete line before the next prompt.
/// @param handle Native handle to write to (typically standardOutput()).
/// @param cursorColumn Current cursor column (1-based). No-op if <= 1.
void emitPartialLineIndicator(NativeHandle handle, int cursorColumn);

} // namespace endo
