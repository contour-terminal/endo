// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/PromptConfig.hpp>
#include <shell/PromptLayoutEngine.hpp>
#include <shell/PromptModule.hpp>

#include <endo-language/DiagnosticsCollector.hpp>
#include <endo-language/HoverInfo.hpp>
#include <endo-language/TokenClassification.hpp>

#include <tui/CompletionPopup.hpp>
#include <tui/Component.hpp>
#include <tui/InputField.hpp>
#include <tui/TextDecorator.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace endo
{

class Completer;
class CommandResolver;
class History;

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

    /// @brief PromptComponent uses I-beam cursor when focused.
    [[nodiscard]] tui::CursorShape cursorShape() const override { return tui::CursorShape::SteadyBar; }

    [[nodiscard]] tui::Size preferredSize() const override;

    // --- Prompt-specific API ---

    /// @brief Sets the prompt string displayed before user input.
    void setPrompt(std::string_view prompt);

    /// @brief Returns the current prompt string.
    [[nodiscard]] std::string_view prompt() const noexcept { return _promptStr; }

    /// @brief Returns the current input text.
    [[nodiscard]] std::string_view text() const noexcept { return _inputField.text(); }

    /// @brief Clears the input field and invalidates caches.
    void clear()
    {
        _inputField.clear();
        _moduleCacheValid = false;
        _highlightCacheText.clear();
        _highlightCacheMap.clear();
        _diagnosticsPendingSince.reset();
        _ghostTextDirty = false;
        _completionPopupDirty = false;
        _ghostTextPendingSince.reset();
        _suggestCacheText.clear();
        _suggestCacheResult.reset();
    }

    /// @brief Adds an entry to the command history.
    void addHistory(std::string entry) { _inputField.addHistory(std::move(entry)); }

    /// @brief Enables or disables multiline editing mode.
    void setMultiline(bool enable) { _inputField.setMultiline(enable); }

    /// @brief Returns whether multiline mode is enabled.
    [[nodiscard]] bool isMultiline() const noexcept { return _inputField.isMultiline(); }

    /// @brief Sets the completer for autocompletion.
    void setCompleter(Completer* completer) { _completer = completer; }

    /// @brief Sets the command resolver for tooltip display.
    void setCommandResolver(CommandResolver* resolver) { _commandResolver = resolver; }

    /// @brief Sets the history source for inline history cycling.
    void setHistory(History const* history) { _history = history; }

    /// @brief Called when a hover is confirmed over this component.
    ///
    /// This is called by the Screen's hover system after the hover delay.
    /// The coordinates are component-relative.
    void onHoverConfirmed(int x, int y);

    /// @brief Called when a hover leaves this component.
    void onHoverLeave();

    /// @brief Returns whether the completion popup is visible.
    [[nodiscard]] bool completionVisible() const noexcept { return _completionPopup.visible(); }

    /// @brief Sets the clipboard callback for copy operations.
    void setClipboardCallback(tui::InputField::ClipboardCallback callback)
    {
        _inputField.setClipboardCallback(std::move(callback));
    }

    /// @brief Sets externally known F# names for diagnostics suppression.
    ///
    /// These names (persisted functions/bindings from prior REPL prompts) will be
    /// forwarded to collectDiagnostics() so they don't trigger "command not found".
    /// @param names The set of known F# names.
    void setKnownFSharpNames(std::set<std::string> names);

    /// @brief Sets the prompt configuration (layout, modules, separator, etc.).
    /// @param config The new prompt configuration.
    void setPromptConfig(PromptConfig config);

    /// @brief Returns the current prompt configuration.
    [[nodiscard]] PromptConfig const& promptConfig() const noexcept { return _config; }

    /// @brief Sets the prompt context (CWD, exit code, etc.) for module evaluation.
    /// @param context The new prompt context.
    void setPromptContext(PromptContext context);

    /// @brief Returns ms until next module refresh, or -1 if no module needs refresh.
    [[nodiscard]] int moduleRefreshTimeoutMs() const;

    /// @brief Returns ms until diagnostics debounce fires, or -1 if no diagnostics pending.
    [[nodiscard]] int diagnosticsTimeoutMs() const;

    /// @brief Returns ms until ghost text debounce fires, or -1 if not pending.
    [[nodiscard]] int ghostTextTimeoutMs() const;

    /// @brief Flushes deferred ghost text and completion popup updates.
    /// Call once per event batch, before drawing.
    void flushDeferredUpdates();

    /// @brief Returns the InputField for direct access.
    [[nodiscard]] tui::InputField& inputField() noexcept { return _inputField; }

    [[nodiscard]] tui::InputField const& inputField() const noexcept { return _inputField; }

    /// @brief Returns the number of chrome lines above input (info line, box frame, etc.).
    [[nodiscard]] int chromeHeight() const noexcept;

    /// @brief Returns the number of rows reserved for the aurora sixel fade (0 or 1).
    [[nodiscard]] int auroraFadeHeight() const noexcept;

    /// @brief Returns the number of top padding rows (0 on first display).
    [[nodiscard]] int topPadding() const noexcept;

    /// @brief Returns the number of bottom padding rows.
    [[nodiscard]] int bottomPadding() const noexcept;

    /// @brief Returns the CompletionPopup for direct access.
    [[nodiscard]] tui::CompletionPopup& completionPopup() noexcept { return _completionPopup; }

    [[nodiscard]] tui::CompletionPopup const& completionPopup() const noexcept { return _completionPopup; }

    /// @brief Result of processing input.
    enum class Action
    {
        None,        ///< No action needed.
        Changed,     ///< Content changed, re-render needed.
        Submit,      ///< User submitted input.
        Abort,       ///< User aborted (Ctrl+C).
        Eof,         ///< User pressed Ctrl+D on empty line.
        ClearScreen, ///< User requested screen clear (Ctrl+L).
        AgentMode,   ///< User pressed '#' on empty prompt to enter agent mode.
    };

    /// @brief Processes an input event and returns the action.
    [[nodiscard]] Action processInput(tui::InputEvent const& event);

  private:
    tui::InputField _inputField;
    tui::CompletionPopup _completionPopup;
    Completer* _completer = nullptr;
    CommandResolver* _commandResolver = nullptr;
    History const* _history = nullptr;
    std::string _promptStr = "> ";

    // Prompt theming
    PromptConfig _config;             ///< Layout and module configuration.
    bool _firstDisplay = true;        ///< Suppresses top padding on the very first render.
    PromptContext _context;           ///< Current shell context for module evaluation.
    PromptLayoutEngine _layoutEngine; ///< Layout rendering engine.
    std::unordered_map<std::string, std::unique_ptr<PromptModule>> _modules; ///< Module registry.

    /// @brief Initializes the module registry with all available modules.
    void initializeModules();

    /// @brief Evaluates configured modules and returns their segments.
    [[nodiscard]] std::vector<PromptSegments> evaluateModules(
        std::vector<std::string> const& moduleNames) const;

    // Style constants
    static constexpr int HorizontalMargin = 1; // Left and right margin
    static constexpr int LeftBarWidth = 1;
    static constexpr int PaddingAfterBar = 1;

    /// @brief Returns the effective left bar width (2 for Rounded separator with ─, 1 otherwise).
    [[nodiscard]] int leftBarWidth() const noexcept
    {
        return (_config.separator == SeparatorStyle::Rounded) ? 2 : LeftBarWidth;
    }

    /// @brief Calculates the width of the prompt prefix (bar + padding + prompt text).
    [[nodiscard]] int promptWidth() const;

    /// @brief Calculates display width of a string.
    [[nodiscard]] static int displayWidth(std::string_view text);

    // Completion helpers
    void updateGhostText();
    void triggerCompletion(bool forceShowPopup);
    void updateCompletionPopup();
    void insertCompletion(std::string_view text);

    /// @brief Hides the completion popup and resets history search mode.
    void dismissPopup();

    // History search (Ctrl+R)
    bool _historySearchMode = false; ///< True when popup shows history entries instead of completions.

    /// @brief Populates the completion popup with fuzzy-searched history entries.
    void triggerHistorySearch();

    /// @brief Re-filters history search popup with updated input text.
    void updateHistorySearchPopup();

    // Tooltip helpers
    /// @brief Gets the command at a screen column position (if any).
    /// @param screenColumn The screen column (0-based).
    /// @return The command string if hovering over command position.
    [[nodiscard]] std::optional<std::string> getCommandAtColumn(int screenColumn) const;

    /// @brief Gets the bounds (start/end column) of the command token.
    /// @return Pair of (start, end) columns in screen coordinates.
    [[nodiscard]] std::pair<int, int> getCommandBounds() const;

    // Module evaluation cache (avoids popen calls during typing)
    std::vector<PromptSegments> _cachedInfoModules;  ///< Cached info line module results.
    std::vector<PromptSegments> _cachedRightModules; ///< Cached right prompt module results.
    bool _moduleCacheValid = false;                  ///< Whether cached module results are current.

    // Syntax highlighting cache (avoids re-tokenizing on every render)
    std::string _highlightCacheText;               ///< Input text that _highlightCacheMap corresponds to.
    std::vector<TokenCategory> _highlightCacheMap; ///< Cached per-grapheme highlight map.

    /// @brief Text decorator that provides syntax highlighting, error underlines, and aurora backgrounds.
    ///
    /// Populated before each render frame and passed to InputField via setTextDecorator().
    /// Uses per-grapheme highlight map and error map, plus per-column aurora background.
    class PromptTextDecorator: public tui::TextDecorator
    {
      public:
        std::vector<TokenCategory> const* highlightMap = nullptr; ///< Per-grapheme TokenCategory.
        std::vector<bool> const* errorMap = nullptr;              ///< Per-grapheme error flags.
        std::vector<tui::RgbColor> const* bgColors = nullptr;     ///< Aurora gradient (per display col).
        tui::RgbColor flatBg {};                                  ///< Fallback background color.
        int bgOffset = 0;                                         ///< Column offset into bgColors.
        tui::Theme const* theme = nullptr;

        [[nodiscard]] auto foreground(tui::TextPosition pos) const -> std::optional<tui::RgbColor> override;
        [[nodiscard]] auto underline(tui::TextPosition pos) const
            -> std::optional<UnderlineDecoration> override;
        [[nodiscard]] auto background(int displayCol) const -> std::optional<tui::RgbColor> override;
    };

    PromptTextDecorator _decorator; ///< Decorator instance, populated per render frame.

    // Deferred update flags (set during batch, flushed once before draw)
    bool _ghostTextDirty = false;       ///< Ghost text needs recomputation.
    bool _completionPopupDirty = false; ///< Completion popup needs re-filtering.

    // Ghost text debounce timer
    std::optional<std::chrono::steady_clock::time_point> _ghostTextPendingSince;
    static constexpr auto GhostTextDebounceMs = std::chrono::milliseconds(100);

    // suggest() result cache (avoids repeated filesystem/history lookups)
    std::string _suggestCacheText;                  ///< Input text the cache corresponds to.
    std::optional<std::string> _suggestCacheResult; ///< Cached suggestion (nullopt = no suggestion).

    // Diagnostics cache for parse error underlines
    std::vector<bool> _errorMap; ///< Per-grapheme error flags for current input.
    std::vector<endo::DiagnosticMessage> _diagnostics;
    std::string _diagnosticsContent;         ///< Input text that _diagnostics corresponds to.
    std::set<std::string> _knownFSharpNames; ///< Persisted F# names from prior REPL prompts.

    // Diagnostics debounce timer
    std::optional<std::chrono::steady_clock::time_point> _diagnosticsPendingSince;
    static constexpr auto DiagnosticsDebounceMs = std::chrono::milliseconds(300);

    /// @brief Recomputes diagnostics if the input text has changed.
    void updateDiagnostics();

    /// @brief Finds a diagnostic at the given 0-based source position.
    /// @param line The 0-based line number.
    /// @param character The 0-based character (codepoint) index.
    /// @return The diagnostic at that position, or std::nullopt.
    [[nodiscard]] std::optional<endo::DiagnosticMessage> diagnosticAt(int line, int character) const;

    /// @brief Converts screen coordinates to a 0-based source position.
    /// @param x The screen column (component-relative).
    /// @param y The screen row (component-relative, = line index).
    /// @return The corresponding source position, or std::nullopt if outside text.
    [[nodiscard]] std::optional<endo::SourcePosition> screenToSourcePosition(int x, int y) const;

    // Module auto-refresh deadline
    std::optional<std::chrono::steady_clock::time_point> _nextModuleRefresh;

    /// @brief Computes the next refresh deadline from active module intervals.
    /// @return The earliest deadline, or std::nullopt if no module needs refresh.
    [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> computeModuleRefreshDeadline() const;

    // Double-Tab detection
    std::chrono::steady_clock::time_point _lastTabTime {};
    static constexpr auto DoubleTabThreshold = std::chrono::milliseconds(400);

    // Inline history cycling (fish-style prefix search)
    std::vector<std::string> _historyCandidates; ///< Cached prefix-matched history entries.
    std::optional<size_t> _historyCycleIndex;    ///< Current position in candidates (nullopt = not cycling).
    std::string _historyCycleSavedInput;         ///< Original input text before cycling started.

    /// @brief Resets inline history cycling state.
    void resetHistoryCycling();

    // Aurora sixel fade cache
    std::string _auroraFadeSixelCache;        ///< Pre-encoded sixel string.
    int _auroraFadeCacheWidth = 0;            ///< Content width (cols) for which cache is valid.
    int _auroraFadeCacheCellW = 0;            ///< Cell pixel width for which cache is valid.
    int _auroraFadeCacheCellH = 0;            ///< Cell pixel height for which cache is valid.
    tui::RgbColor _auroraFadeCacheBgColor {}; ///< Background color for which cache is valid.

    /// @brief Generates a pre-encoded sixel string for the aurora fade effect.
    /// @param cellPixelWidth Pixel width per cell.
    /// @param cellPixelHeight Pixel height per cell.
    /// @param contentWidthCols Content width in columns (excluding margins).
    /// @return Pre-encoded sixel string, or empty on failure.
    [[nodiscard]] std::string generateAuroraFadeSixel(int cellPixelWidth,
                                                      int cellPixelHeight,
                                                      int contentWidthCols,
                                                      tui::RgbColor bgColor) const;
};

} // namespace endo
