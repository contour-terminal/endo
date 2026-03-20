// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tui/Buffer.hpp>
#include <tui/Component.hpp>
#include <tui/CursorShape.hpp>
#include <tui/HoverState.hpp>
#include <tui/Theme.hpp>
#include <tui/Tooltip.hpp>

#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace tui
{

class Terminal;

/// Viewport mode for the screen.
enum class Viewport : uint8_t
{
    Fullscreen, ///< Uses entire terminal, clears on each frame.
    Inline,     ///< Renders at current cursor position, grows downward.
    Fixed,      ///< Fixed region of the terminal.
};

/// Render mode for performance testing.
enum class RenderMode : uint8_t
{
    Diff, ///< Only write changed cells (default).
    Full, ///< Always write all cells (for benchmarking).
};

/// Unscroll mode for inline viewport.
///
/// Controls whether to use the Kitty unscroll extension (CSI Ps + T) to
/// restore scrollback content when inline content shrinks (e.g., popup hides).
/// Supported by: Kitty, Contour, mintty.
enum class UnscrollMode : uint8_t
{
    Auto,     ///< Auto-detect terminal support (default).
    Enabled,  ///< Always use unscroll (for known-supported terminals).
    Disabled, ///< Never use unscroll (fallback to clearing rows).
};

/// Configuration for the Screen.
struct ScreenConfig
{
    Viewport viewport = Viewport::Fullscreen;
    Rect fixedArea;                                 ///< For Viewport::Fixed.
    int inlineMaxHeight = 0;                        ///< For Viewport::Inline (0 = no limit).
    UnscrollMode unscrollMode = UnscrollMode::Auto; ///< Unscroll behavior for inline mode.
    bool inhibitReflow = false;                     ///< Disable text reflow (DEC 2028) on rendered lines.
};

/// Cursor movement calculations for inline rendering.
///
/// This struct captures the cursor movements needed when transitioning
/// between renders with different content heights in inline viewport mode.
struct InlineCursorMovement
{
    int moveUpToStart = 0;       ///< Move up from previous cursor position to row 0.
    int newLinesToEmit = 0;      ///< Newlines to emit when content grows.
    int moveUpAfterNewlines = 0; ///< Move up after emitting newlines (equals newLinesToEmit).
    int rowsToClear = 0;         ///< Rows to clear when content shrinks.
};

/// Calculates cursor movements for inline rendering transitions.
///
/// @param previousContentHeight Height of content from previous render.
/// @param previousCursorRow Cursor row at end of previous render.
/// @param newContentHeight Height of content for current render.
/// @return Cursor movement instructions.
[[nodiscard]] InlineCursorMovement calculateInlineCursorMovement(int previousContentHeight,
                                                                 int previousCursorRow,
                                                                 int newContentHeight);

/// Central coordinator for the TUI component system.
///
/// Screen manages the component tree, rendering, event dispatch, and focus.
/// It owns the root component and coordinates all rendering through a
/// double-buffered diff system for efficient terminal updates.
///
/// Usage:
/// @code
/// tui::Terminal terminal;
/// tui::Screen screen(terminal);
///
/// // Add components to root
/// MyComponent myComp;
/// screen.root().addChild(myComp, {.area = {0, 0, 40, 10}});
///
/// // Main loop
/// while (running) {
///     screen.draw();  // Render and flush
///     for (auto& event : terminal.poll())
///         screen.dispatchEvent(event);
/// }
/// @endcode
class Screen // NOLINT(clang-analyzer-optin.performance.Padding)
{
  public:
    /// Constructs a Screen attached to the given terminal.
    /// @param terminal The terminal for input/output.
    /// @param config Optional configuration.
    explicit Screen(Terminal& terminal, ScreenConfig config = {});

    ~Screen();

    // Prevent copying
    Screen(Screen const&) = delete;
    Screen& operator=(Screen const&) = delete;

    // --- Root Component ---

    /// Returns the root component. Add top-level components as children.
    [[nodiscard]] Component& root() noexcept;
    [[nodiscard]] Component const& root() const noexcept;

    // --- Rendering ---

    /// Renders the component tree to the terminal.
    /// Performs diff-based update (or full update if RenderMode::Full).
    void draw();

    /// Marks the entire screen as needing a full redraw.
    void invalidate();

    /// Marks a specific component as needing a redraw.
    void invalidate(Component& component);

    /// Releases cursor tracking state after external output.
    ///
    /// Call this when external processes have written to the terminal (e.g., after
    /// running shell commands). The cursor position is no longer known, so the next
    /// draw() in inline mode will start fresh instead of trying to move relative
    /// to where it thinks the cursor is.
    void releaseCursor();

    /// Clears content from the tracked cursor position and releases cursor tracking.
    ///
    /// Moves up to the top of the inline content region, clears from there to the
    /// end of the display, and calls releaseCursor(). Use this instead of manual
    /// moveUp() + clear + releaseCursor() sequences to avoid cursor drift.
    void clearAndRelease();

    // --- Render Mode ---

    /// Sets the render mode (Diff or Full).
    void setRenderMode(RenderMode mode) { _renderMode = mode; }

    /// Returns the current render mode.
    [[nodiscard]] RenderMode renderMode() const noexcept { return _renderMode; }

    // --- Theme ---

    /// Sets the theme for all components.
    void setTheme(Theme theme);

    /// Returns the current theme.
    [[nodiscard]] Theme const& theme() const noexcept { return _theme; }

    // --- Dimensions ---

    /// Returns the number of rows available for rendering.
    [[nodiscard]] int rows() const noexcept;

    /// Returns the number of columns available for rendering.
    [[nodiscard]] int cols() const noexcept;

    /// Returns the size of the rendering area.
    [[nodiscard]] Size size() const noexcept;

    // --- Viewport ---

    /// Sets the viewport mode.
    void setViewport(Viewport viewport);

    /// Sets a fixed viewport area.
    void setViewport(Rect fixedArea);

    /// Returns the current viewport mode.
    [[nodiscard]] Viewport viewport() const noexcept { return _config.viewport; }

    /// Returns the current viewport area in terminal coordinates.
    [[nodiscard]] Rect viewportArea() const noexcept;

    // --- Event Dispatch ---

    /// Dispatches an event through the component tree.
    /// For mouse events: hit tests to find target, then bubbles up.
    /// For keyboard events: sends to focused component, then bubbles up.
    /// @param event The event to dispatch.
    /// @return The result of event handling.
    [[nodiscard]] EventResult dispatchEvent(InputEvent const& event);

    // --- Focus Groups ---

    /// Sets the active focus group.
    /// Only the active group receives keyboard events.
    void setActiveGroup(FocusGroupId const& group);

    /// Returns the active focus group.
    [[nodiscard]] FocusGroupId const& activeGroup() const noexcept { return _activeGroup; }

    /// Sets the focused component within a focus group.
    void setFocus(Component* component);

    /// Sets the focused component within a specific focus group.
    void setFocus(FocusGroupId const& group, Component* component);

    /// Returns the focused component in the active group.
    [[nodiscard]] Component* focusedComponent() const noexcept;

    /// Returns the focused component in a specific group.
    [[nodiscard]] Component* focusedComponent(FocusGroupId const& group) const noexcept;

    /// Moves focus to the next focusable component in the active group.
    void focusNext();

    /// Moves focus to the previous focusable component in the active group.
    void focusPrev();

    // --- Terminal Access ---

    /// Returns the terminal.
    [[nodiscard]] Terminal& terminal() noexcept { return _terminal; }

    [[nodiscard]] Terminal const& terminal() const noexcept { return _terminal; }

    // --- Overlay System ---

    /// Shows an overlay component at the given absolute screen position.
    ///
    /// Overlays are rendered above all regular components and are not clipped
    /// by their logical parent's bounds. Use this for popups, tooltips, menus, etc.
    ///
    /// @param overlay The overlay component to show.
    /// @param position Absolute screen position (top-left corner of overlay).
    void showOverlay(Component& overlay, Point position);

    /// Hides/removes an overlay from the screen.
    /// @param overlay The overlay to hide.
    void hideOverlay(Component& overlay);

    /// Updates an overlay's position.
    /// @param overlay The overlay to reposition.
    /// @param position New absolute screen position.
    void positionOverlay(Component& overlay, Point position);

    /// Returns true if the overlay is currently visible.
    [[nodiscard]] bool isOverlayVisible(Component const& overlay) const noexcept;

    // --- Hover and Tooltip System ---

    /// Returns the hover state manager.
    [[nodiscard]] HoverState& hoverState() noexcept { return _hoverState; }

    [[nodiscard]] HoverState const& hoverState() const noexcept { return _hoverState; }

    /// Returns the recommended poll timeout in ms, considering hover state.
    ///
    /// Returns -1 if no timeout needed (block indefinitely).
    /// Returns 0 if immediate processing needed.
    /// Returns >0 for hover delay timeout.
    [[nodiscard]] int pollTimeoutMs() const;

    /// Processes hover timer tick. Call this when poll times out with no events.
    void tickHover();

    /// Shows a tooltip at the specified position.
    /// @param text Tooltip content.
    /// @param position Absolute screen position for tooltip.
    /// @param contentType Content type (plain text or markdown).
    void showTooltip(std::string_view text,
                     Point position,
                     TooltipContentType contentType = TooltipContentType::PlainText);

    /// Hides the current tooltip.
    void hideTooltip();

    /// Returns true if a tooltip is currently visible.
    [[nodiscard]] bool isTooltipVisible() const noexcept { return _tooltipVisible; }

    /// Returns the last-rendered buffer (for testing). Valid after draw().
    [[nodiscard]] Buffer const& renderedBuffer() const noexcept { return _previous; }

  private:
    Terminal& _terminal;
    ScreenConfig _config;
    Theme _theme;
    RenderMode _renderMode = RenderMode::Diff;

    std::unique_ptr<RootComponent> _root;
    Buffer _current;
    Buffer _previous;

    bool _needsFullRedraw = true;
    std::unordered_set<Component*> _dirtyComponents;
    int _previousContentHeight = 0;
    int _previousCursorRow = 0;         ///< Cursor row at end of last inline render (for positioning)
    int _inlineContentStartRow = -1;    ///< Terminal row (0-based) where inline content starts (-1 = unknown)
    int _mainContentHeight = 0;         ///< Content height before overlays (for mouse coordinate translation)
    int _previousMainContentHeight = 0; ///< Previous main content height (for detecting real content growth)
    int _peakContentHeight = 0;         ///< High water mark - max content height we've allocated space for
    int _totalNewlinesEmitted = 0;      ///< Total newlines emitted since start (for tracking content shift)
    // NOTE: Unscroll feature is disabled until properly implemented for inline mode.
    // The _scrolledLinesCount and _unscrollSupported fields are kept for future use.
    // int _scrolledLinesCount = 0;   ///< Lines scrolled into scrollback (for unscroll)
    // bool _unscrollSupported = false; ///< Resolved unscroll support (from config + detection)

    // Focus state
    FocusGroupId _activeGroup = std::string(DefaultFocusGroup);
    std::unordered_map<FocusGroupId, Component*> _focusedComponents;
    CursorShape _currentCursorShape = CursorShape::Default; ///< Last applied cursor shape

    // Overlay state
    struct OverlayEntry
    {
        Component* component = nullptr;
        Point position;
    };

    std::vector<OverlayEntry> _overlays;

    // Hover and Tooltip state
    HoverState _hoverState;
    Tooltip _tooltip;
    bool _tooltipVisible = false;

    // Rendering phases
    void beginFrame();
    void renderTree();
    void renderComponent(Component& component, Rect parentBounds);
    void renderOverlays();
    void endFrame();
    void flush();
    void flushFullscreen();
    void flushInline();
    void flushFixed();
    void applyCursorShape(); ///< Applies cursor shape based on focused component.

    // Hit testing
    [[nodiscard]] Component* componentAt(int row, int col) const;
    [[nodiscard]] Component* componentAtRecursive(Component& component, int row, int col) const;

    // Event dispatch helpers
    [[nodiscard]] static EventResult bubbleEvent(Component* target, InputEvent const& event);
    [[nodiscard]] EventResult dispatchKeyEvent(InputEvent const& event);
    [[nodiscard]] EventResult dispatchMouseEvent(MouseEvent const& mouse);

    // Focus helpers
    static void updateFocus(Component* oldFocus, Component* newFocus);
    [[nodiscard]] std::vector<Component*> collectFocusableComponents(FocusGroupId const& group) const;
    void collectFocusableRecursive(Component& component,
                                   FocusGroupId const& group,
                                   std::vector<Component*>& out) const;
};

} // namespace tui
