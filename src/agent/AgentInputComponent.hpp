// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tui/CompletionPopup.hpp>
#include <tui/Component.hpp>
#include <tui/InputField.hpp>
#include <tui/completer/Completer.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <string>

namespace tui
{
class CompletionProvider;
} // namespace tui

namespace endo::agent
{

/// Styled input component for agent mode queries.
///
/// Renders a rounded-chrome header line with agent mode label, provider, and model info,
/// followed by the input area with a configurable prompt indicator. Uses the agent color
/// palette (purple accent) to visually distinguish from the shell prompt.
///
/// Supports slash command completion via a CompletionPopup and Completer, reusing the
/// same TUI completion infrastructure as the shell prompt.
class AgentInputComponent: public tui::Component
{
  public:
    /// @brief Result of processing input.
    enum class Action
    {
        None,      ///< No action needed.
        Changed,   ///< Content changed, re-render needed.
        Submit,    ///< User pressed Enter on non-empty input.
        Abort,     ///< User pressed Escape to exit agent mode.
        CycleMode, ///< User toggled agent sub-mode (plan/execute).
    };

    AgentInputComponent();
    ~AgentInputComponent() override = default;

    // --- Component Interface ---

    void render(tui::Canvas& canvas) override;
    [[nodiscard]] tui::EventResult onEvent(tui::InputEvent const& event) override;

    [[nodiscard]] bool focusable() const override { return true; }

    [[nodiscard]] tui::CursorShape cursorShape() const override { return tui::CursorShape::SteadyBar; }

    [[nodiscard]] tui::Size preferredSize() const override;

    // --- Agent Input API ---

    /// @brief Processes an input event and returns the action.
    /// @param event The input event to process.
    /// @return The resulting action.
    [[nodiscard]] Action processInput(tui::InputEvent const& event);

    /// @brief Sets the prompt indicator displayed before user input.
    /// @param indicator The indicator string (e.g., "❯").
    void setPromptIndicator(std::string indicator);

    /// @brief Sets the provider name displayed in the header line.
    /// @param name The provider name (e.g., "claude", "openai").
    void setProviderName(std::string name) { _providerName = std::move(name); }

    /// @brief Sets the model name displayed in the header line.
    /// @param name The model identifier (e.g., "claude-sonnet-4-5-20250929").
    void setModelName(std::string name) { _modelName = std::move(name); }

    /// @brief Sets the git branch name displayed in the header line.
    /// @param branch The branch name (e.g., "main", "feature/xyz").
    void setGitBranch(std::string branch) { _gitBranch = std::move(branch); }

    /// @brief Sets the project path displayed in the header line (tilde-contracted).
    /// @param path The project path (e.g., "~/projects/endo").
    void setProjectPath(std::string path) { _projectPath = std::move(path); }

    /// @brief Returns the current input text.
    [[nodiscard]] auto text() const noexcept -> std::string_view { return _inputField.text(); }

    /// @brief Clears the input field and dismisses any popup.
    void clear()
    {
        _inputField.clear();
        dismissPopup();
    }

    /// @brief Sets whether plan mode is active (affects header badge).
    /// @param enabled True to show plan mode, false for execute mode.
    void setPlanMode(bool enabled) { _planMode = enabled; }

    /// @brief Returns whether plan mode is currently active.
    [[nodiscard]] bool planMode() const noexcept { return _planMode; }

    /// @brief Sets the number of blank rows above the component content.
    /// @param padding Number of blank rows (typically 0 or 1, from promptSpacing).
    void setTopPadding(int padding) noexcept { _topPadding = padding; }

    /// @brief Returns the current top padding.
    [[nodiscard]] int topPadding() const noexcept { return _topPadding; }

    /// @brief Returns the InputField for direct access.
    [[nodiscard]] auto inputField() noexcept -> tui::InputField& { return _inputField; }

    [[nodiscard]] auto inputField() const noexcept -> tui::InputField const& { return _inputField; }

    // --- Completion API ---

    /// @brief Adds a completion provider to the completer.
    /// @param provider The provider to add (ownership transferred).
    void addCompletionProvider(std::unique_ptr<tui::CompletionProvider> provider);

    /// @brief Returns whether the completion popup is currently visible.
    [[nodiscard]] bool completionVisible() const noexcept { return _completionPopup.visible(); }

    /// @brief Returns the CompletionPopup for direct access.
    [[nodiscard]] tui::CompletionPopup& completionPopup() noexcept { return _completionPopup; }

    /// @brief Flushes deferred completion popup and ghost text updates.
    /// Call once per event batch, before drawing.
    void flushDeferredUpdates();

    /// @brief Returns milliseconds until the ghost text debounce fires, or -1 if idle.
    ///
    /// Use this to set the event loop poll timeout so ghost text appears promptly
    /// after the debounce period without requiring additional keystrokes.
    [[nodiscard]] int ghostTextTimeoutMs() const;

  private:
    tui::InputField _inputField;
    tui::CompletionPopup _completionPopup; ///< Popup widget for slash command completion.
    tui::Completer _completer;             ///< Orchestrates completion providers.
    bool _completionPopupDirty = false;    ///< Completion popup needs re-filtering.

    std::string _providerName; ///< Active provider name for header display.
    std::string _modelName;    ///< Active model name for header display.
    std::string _gitBranch;    ///< Current git branch for header display.
    std::string _projectPath;  ///< Tilde-contracted project path for header display.
    bool _planMode = false;    ///< Whether plan mode is active (vs execute mode).
    int _topPadding = 0;       ///< Blank rows above content (from promptSpacing).

    static constexpr int LeftBarWidth = 2; ///< Width of the left bar chrome (╭─, ╰─, │).
    static constexpr int BarPadding = 1;   ///< Padding after the bar.
    static constexpr int HeaderHeight = 1; ///< Height of the header line (shows agent/provider/model).

    // Completion helpers
    void triggerCompletion(bool forceShowPopup);
    void updateCompletionPopup();
    void insertCompletion(std::string_view text);
    void dismissPopup();

    /// @brief Checks whether the cursor is inside an @-mention context.
    [[nodiscard]] static bool isInAtMentionContext(std::string_view input, size_t cursorPosition);

    // Ghost text helpers
    void updateGhostText();

    bool _ghostTextDirty = false; ///< Ghost text needs recomputation.
    std::optional<std::chrono::steady_clock::time_point> _ghostTextPendingSince;
    static constexpr auto GhostTextDebounceMs = std::chrono::milliseconds(100);
    std::string _suggestCacheText;                  ///< Last input text for suggest cache.
    std::optional<std::string> _suggestCacheResult; ///< Cached suggest result.
};

} // namespace endo::agent
