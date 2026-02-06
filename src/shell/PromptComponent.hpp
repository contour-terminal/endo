// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

#include <tui/CompletionPopup.hpp>
#include <tui/Component.hpp>
#include <tui/InputField.hpp>

namespace endo
{

class Completer;

/// @brief A styled prompt component for the shell.
///
/// PromptComponent renders an OpenCode-inspired prompt with:
/// - A soft blue left bar
/// - A gray background
/// - Prompt text on the first line
/// - Continuation indicators on subsequent lines
/// - An embedded InputField for text editing
/// - A CompletionPopup for autocompletion
///
/// This component is designed to work with Screen's Inline viewport mode,
/// rendering at the current cursor position and growing downward.
class PromptComponent: public tui::Component
{
  public:
    PromptComponent();
    ~PromptComponent() override = default;

    // --- Component Interface ---

    void render(tui::Canvas& canvas) override;
    [[nodiscard]] tui::EventResult onEvent(tui::InputEvent const& event) override;

    [[nodiscard]] bool focusable() const override { return true; }

    [[nodiscard]] tui::Size preferredSize() const override;

    // --- Prompt-specific API ---

    /// @brief Sets the prompt string displayed before user input.
    void setPrompt(std::string_view prompt);

    /// @brief Returns the current prompt string.
    [[nodiscard]] std::string_view prompt() const noexcept { return _promptStr; }

    /// @brief Returns the current input text.
    [[nodiscard]] std::string_view text() const noexcept { return _inputField.text(); }

    /// @brief Clears the input field.
    void clear() { _inputField.clear(); }

    /// @brief Adds an entry to the command history.
    void addHistory(std::string entry) { _inputField.addHistory(std::move(entry)); }

    /// @brief Enables or disables multiline editing mode.
    void setMultiline(bool enable) { _inputField.setMultiline(enable); }

    /// @brief Returns whether multiline mode is enabled.
    [[nodiscard]] bool isMultiline() const noexcept { return _inputField.isMultiline(); }

    /// @brief Sets the completer for autocompletion.
    void setCompleter(Completer* completer) { _completer = completer; }

    /// @brief Returns whether the completion popup is visible.
    [[nodiscard]] bool completionVisible() const noexcept { return _completionPopup.visible(); }

    /// @brief Sets the clipboard callback for copy operations.
    void setClipboardCallback(tui::InputField::ClipboardCallback callback)
    {
        _inputField.setClipboardCallback(std::move(callback));
    }

    /// @brief Returns the InputField for direct access.
    [[nodiscard]] tui::InputField& inputField() noexcept { return _inputField; }

    [[nodiscard]] tui::InputField const& inputField() const noexcept { return _inputField; }

    /// @brief Returns the CompletionPopup for direct access.
    [[nodiscard]] tui::CompletionPopup& completionPopup() noexcept { return _completionPopup; }

    [[nodiscard]] tui::CompletionPopup const& completionPopup() const noexcept { return _completionPopup; }

    /// @brief Result of processing input.
    enum class Action
    {
        None,    ///< No action needed.
        Changed, ///< Content changed, re-render needed.
        Submit,  ///< User submitted input.
        Abort,   ///< User aborted (Ctrl+C).
        Eof,     ///< User pressed Ctrl+D on empty line.
    };

    /// @brief Processes an input event and returns the action.
    [[nodiscard]] Action processInput(tui::InputEvent const& event);

  private:
    tui::InputField _inputField;
    tui::CompletionPopup _completionPopup;
    Completer* _completer = nullptr;
    std::string _promptStr = "> ";

    // Style constants (OpenCode-inspired)
    static constexpr auto LeftBarColor = tui::RgbColor { .r = 97, .g = 175, .b = 239 };     // Soft blue
    static constexpr auto BackgroundColor = tui::RgbColor { .r = 45, .g = 50, .b = 55 };    // Soft gray
    static constexpr auto PromptTextColor = tui::RgbColor { .r = 180, .g = 180, .b = 180 }; // Light gray
    static constexpr auto InputTextColor = tui::RgbColor { .r = 220, .g = 220, .b = 220 };  // Brighter
    static constexpr int HorizontalMargin = 1; // Left and right margin
    static constexpr int LeftBarWidth = 1;
    static constexpr int PaddingAfterBar = 1;

    /// @brief Calculates the width of the prompt prefix (bar + padding + prompt text).
    [[nodiscard]] int promptWidth() const;

    /// @brief Calculates display width of a string.
    [[nodiscard]] static int displayWidth(std::string_view text);

    // Completion helpers
    void updateGhostText();
    void triggerCompletion();
    void insertCompletion(std::string_view text);
};

} // namespace endo
