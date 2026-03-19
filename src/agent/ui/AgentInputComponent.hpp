// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tui/CommandPalettePopup.hpp>
#include <tui/CommandRegistry.hpp>
#include <tui/CompletionPopup.hpp>
#include <tui/Component.hpp>
#include <tui/InputField.hpp>
#include <tui/Spinner.hpp>
#include <tui/completer/Completer.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <agent/Types.hpp>

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
    enum class Action // NOLINT(performance-enum-size)
    {
        None,              ///< No action needed.
        Changed,           ///< Content changed, re-render needed.
        Submit,            ///< User pressed Enter on non-empty input.
        Abort,             ///< User pressed Escape to exit agent mode.
        CycleMode,         ///< User toggled agent sub-mode (plan/execute).
        CycleThinkingMode, ///< User cycled thinking mode (off/normal/extended).
        CycleModel,        ///< User cycled through available models.
        ClearScreen,       ///< User requested screen clear (Ctrl+L).
        CommandPalette,    ///< User pressed Ctrl+Shift+P to open the command palette.
        NewPrompt,         ///< User requested a new prompt without executing current input.
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

    /// @brief Clears the input field, dismisses any popup, and removes attached images.
    void clear()
    {
        _inputField.clear();
        dismissPopup();
        clearImages();
    }

    /// @brief Sets whether plan mode is active (affects header badge).
    /// @param enabled True to show plan mode, false for execute mode.
    void setPlanMode(bool enabled) { _planMode = enabled; }

    /// @brief Returns whether plan mode is currently active.
    [[nodiscard]] bool planMode() const noexcept { return _planMode; }

    /// @brief Sets the thinking mode displayed in the header line.
    /// @param mode The active thinking mode.
    void setThinkingMode(ThinkingMode mode) { _thinkingMode = mode; }

    /// @brief Returns the current thinking mode.
    [[nodiscard]] ThinkingMode thinkingMode() const noexcept { return _thinkingMode; }

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

    /// @brief Returns the CommandPalettePopup for direct access.
    [[nodiscard]] tui::CommandPalettePopup& commandPalette() noexcept { return _commandPalette; }

    /// @brief Sets the command registry used by the command palette.
    /// @param registry Pointer to the registry (caller owns, must outlive this component).
    void setCommandRegistry(tui::CommandRegistry* registry) { _commandRegistry = registry; }

    /// @brief Flushes deferred completion popup and ghost text updates.
    /// Call once per event batch, before drawing.
    void flushDeferredUpdates();

    /// @brief Returns milliseconds until the ghost text debounce fires, or -1 if idle.
    ///
    /// Use this to set the event loop poll timeout so ghost text appears promptly
    /// after the debounce period without requiring additional keystrokes.
    [[nodiscard]] int ghostTextTimeoutMs() const;

    /// @brief Returns milliseconds until the Escape hint expires, or -1 if idle.
    ///
    /// Use this to set the event loop poll timeout so the hint auto-restores
    /// without requiring additional keystrokes.
    [[nodiscard]] int escapeHintTimeoutMs() const;

    // --- Image Attachment API ---

    /// @brief Attaches an image from raw bytes.
    /// @param data Raw image bytes (PNG, JPEG, etc.).
    /// @param mediaType MIME type (e.g. "image/png").
    /// @return true if attached, false if the maximum limit was reached.
    bool attachImage(std::vector<std::uint8_t> data, std::string mediaType);

    /// @brief Removes the image at the given index.
    /// @param index Zero-based index of the image to remove.
    void removeImage(size_t index);

    /// @brief Removes all attached images.
    void clearImages();

    /// @brief Returns attached images for submission.
    [[nodiscard]] auto attachedImages() const noexcept -> std::span<ImageBlock const>;

    /// @brief Returns the number of attached images.
    [[nodiscard]] auto imageCount() const noexcept -> size_t;

    /// @brief Sets the cell pixel dimensions for preview sizing.
    /// @param width Cell width in pixels.
    /// @param height Cell height in pixels.
    void setCellPixelDimensions(int width, int height) noexcept;

    /// @brief Returns the number of terminal rows occupied by image previews.
    [[nodiscard]] int imagePreviewHeight() const noexcept;

    // --- Thinking/Activity State ---

    /// @brief Sets whether the agent is actively thinking/processing.
    ///
    /// When active, the info line below the input shows a spinner with the activity label.
    /// When inactive, the info line shows shortcut hints.
    /// @param active True to show spinner, false to show hints.
    void setThinkingActive(bool active);

    /// @brief Returns whether the agent is actively thinking.
    [[nodiscard]] bool thinkingActive() const noexcept { return _thinkingActive; }

    /// @brief Sets the activity label shown next to the spinner.
    /// @param label The label text (e.g., "Thinking...", "Running shell_execute...").
    void setActivityLabel(std::string label);

    /// @brief Advances the spinner animation.
    /// @return True if the frame changed and a re-render is needed.
    [[nodiscard]] bool tickSpinner();

    /// @brief Returns milliseconds until the next spinner frame, or -1 if not active.
    [[nodiscard]] int spinnerTimeoutMs() const;

  private:
    tui::InputField _inputField;
    tui::CompletionPopup _completionPopup;            ///< Popup widget for slash command completion.
    tui::CommandPalettePopup _commandPalette;         ///< Command palette popup.
    tui::CommandRegistry* _commandRegistry = nullptr; ///< External command registry.
    tui::Completer _completer;                        ///< Orchestrates completion providers.
    bool _completionPopupDirty = false;               ///< Completion popup needs re-filtering.

    std::string _providerName;                      ///< Active provider name for header display.
    std::string _modelName;                         ///< Active model name for header display.
    std::string _gitBranch;                         ///< Current git branch for header display.
    std::string _projectPath;                       ///< Tilde-contracted project path for header display.
    bool _planMode = false;                         ///< Whether plan mode is active (vs execute mode).
    ThinkingMode _thinkingMode = ThinkingMode::Off; ///< Active thinking mode for header display.
    int _topPadding = 0;                            ///< Blank rows above content (from promptSpacing).

    static constexpr int LeftBarWidth = 2;   // NOLINT(readability-identifier-naming)
    static constexpr int BarPadding = 1;     // NOLINT(readability-identifier-naming)
    static constexpr int HeaderHeight = 1;   // NOLINT(readability-identifier-naming)
    static constexpr int InfoLineHeight = 1; // NOLINT(readability-identifier-naming)
    static constexpr int BottomPadding = 1;  // NOLINT(readability-identifier-naming)
    // NOLINTNEXTLINE(readability-identifier-naming)
    static constexpr int FooterHeight = InfoLineHeight + BottomPadding;

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
    // NOLINTNEXTLINE(readability-identifier-naming)
    static constexpr auto GhostTextDebounceMs = std::chrono::milliseconds(100);
    std::string _suggestCacheText;                  ///< Last input text for suggest cache.
    std::optional<std::string> _suggestCacheResult; ///< Cached suggest result.

    // Info line rendering
    void renderInfoLine(tui::Canvas& canvas, int row);

    // Thinking/activity state
    tui::Spinner _spinner { tui::SpinnerType::Dots }; ///< Spinner for thinking animation.
    bool _thinkingActive = false;                     ///< Whether agent is thinking/processing.
    std::string _activityLabel;                       ///< Label shown next to spinner.

    // Escape double-press confirmation state
    void restoreFromEscapeHint();

    // NOLINTNEXTLINE(readability-identifier-naming)
    static constexpr auto EscapeHintTimeout = std::chrono::milliseconds(1000);
    std::chrono::steady_clock::time_point _lastEscapeTime;
    bool _escapeHintVisible = false;
    std::string _savedTextBeforeEscape;
    size_t _savedCursorBeforeEscape = 0;

    /// @brief Cached preview layout dimensions for a single image.
    struct PreviewLayout
    {
        int colSpan = 0;  ///< Width in terminal columns.
        int lineSpan = 1; ///< Height in terminal lines.
    };

    // Image attachment state
    std::vector<ImageBlock> _attachedImages;                  ///< Images attached via paste.
    std::vector<std::string> _imagePreviews;                  ///< Pre-encoded sixel data for preview.
    std::vector<PreviewLayout> _previewLayouts;               ///< Cached layout per preview image.
    int _cellPixelWidth = 0;                                  ///< Terminal cell width in pixels.
    int _cellPixelHeight = 0;                                 ///< Terminal cell height in pixels.
    static constexpr int MaxAttachedImages = 5;               // NOLINT(readability-identifier-naming)
    static constexpr int PreviewMaxColumns = 30;              // NOLINT(readability-identifier-naming)
    static constexpr int PreviewMaxLines = 8;                 // NOLINT(readability-identifier-naming)
    static constexpr size_t MaxImageBytes = 10 * 1024 * 1024; // NOLINT(readability-identifier-naming)
};

} // namespace endo::agent
