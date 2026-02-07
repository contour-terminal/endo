// SPDX-License-Identifier: Apache-2.0
#include "Screen.hpp"

#include <algorithm>
#include <chrono>

#include <tui/Canvas.hpp>
#include <tui/Terminal.hpp>

namespace tui
{

InlineCursorMovement calculateInlineCursorMovement(int previousContentHeight,
                                                   int previousCursorRow,
                                                   int newContentHeight)
{
    InlineCursorMovement result;

    // Move from previous cursor position to row 0
    if (previousContentHeight > 0 && previousCursorRow > 0)
        result.moveUpToStart = previousCursorRow;

    // Calculate newlines needed when content grows
    if (newContentHeight > previousContentHeight)
    {
        result.newLinesToEmit = newContentHeight - previousContentHeight;
        // Move back up by the number of newlines emitted (NOT contentHeight!)
        result.moveUpAfterNewlines = result.newLinesToEmit;
    }

    // Calculate rows to clear when content shrinks
    if (previousContentHeight > newContentHeight && newContentHeight > 0)
        result.rowsToClear = previousContentHeight - newContentHeight;

    return result;
}

Screen::Screen(Terminal& terminal, ScreenConfig config):
    _terminal(terminal), _config(config), _theme(darkTheme()), _root(std::make_unique<RootComponent>())
{
    _root->setScreen(this);

    // Initialize buffers based on terminal size
    auto const termRows = _terminal.rows();
    auto const termCols = _terminal.columns();
    _current.resize(termRows, termCols);
    _previous.resize(termRows, termCols);

    // NOTE: Unscroll feature (CSI n + T) is currently disabled for inline mode because
    // the sequence shifts the ENTIRE screen down, which doesn't work well when rendering
    // at the bottom of the terminal. The feature needs a different approach for inline
    // rendering, possibly using scroll regions.
    // The UnscrollMode config is preserved for future implementation.
    (void) _config.unscrollMode; // Suppress unused warning
}

Screen::~Screen() = default;

Component& Screen::root() noexcept
{
    return *_root;
}

Component const& Screen::root() const noexcept
{
    return *_root;
}

void Screen::draw()
{
    beginFrame();
    renderTree();

    // Calculate main content height BEFORE overlays for mouse coordinate translation
    if (_config.viewport == Viewport::Inline)
    {
        _previousMainContentHeight = _mainContentHeight;
        _mainContentHeight = 0;
        for (int row = _current.rows() - 1; row >= 0; --row)
        {
            bool hasContent = false;
            for (int col = 0; col < _current.cols(); ++col)
            {
                Cell const& cell = _current.at(row, col);
                if (!cell.grapheme.empty() && cell.grapheme != " ")
                {
                    hasContent = true;
                    break;
                }
            }
            if (hasContent)
            {
                _mainContentHeight = row + 1;
                break;
            }
        }
    }

    renderOverlays();
    endFrame();
    flush();
}

void Screen::invalidate()
{
    _needsFullRedraw = true;
}

void Screen::invalidate([[maybe_unused]] Component& component)
{
    // For now, just mark full redraw needed.
    // Future optimization: track dirty regions per component.
    _needsFullRedraw = true;
}

void Screen::releaseCursor()
{
    // Reset cursor tracking for inline mode.
    // After external output (e.g., shell command output), the cursor position
    // is no longer where we think it is. Reset to 0 so next draw() starts fresh.
    _previousContentHeight = 0;
    _previousCursorRow = 0;
    _peakContentHeight = 0;
    _inlineContentStartRow = -1;
    _totalNewlinesEmitted = 0;
    _needsFullRedraw = true;
    // Reset cursor shape tracking so we re-apply it on next flush
    _currentCursorShape = CursorShape::Default;
}

void Screen::setTheme(Theme theme)
{
    _theme = std::move(theme);
    invalidate();
}

int Screen::rows() const noexcept
{
    switch (_config.viewport)
    {
        case Viewport::Fullscreen: return _terminal.rows();
        case Viewport::Inline:
            return _config.inlineMaxHeight > 0 ? _config.inlineMaxHeight : _terminal.rows();
        case Viewport::Fixed: return _config.fixedArea.height;
    }
    return _terminal.rows();
}

int Screen::cols() const noexcept
{
    switch (_config.viewport)
    {
        case Viewport::Fullscreen: return _terminal.columns();
        case Viewport::Inline: return _terminal.columns();
        case Viewport::Fixed: return _config.fixedArea.width;
    }
    return _terminal.columns();
}

Size Screen::size() const noexcept
{
    return { cols(), rows() };
}

void Screen::setViewport(Viewport viewport)
{
    _config.viewport = viewport;
    invalidate();
}

void Screen::setViewport(Rect fixedArea)
{
    _config.viewport = Viewport::Fixed;
    _config.fixedArea = fixedArea;
    invalidate();
}

Rect Screen::viewportArea() const noexcept
{
    switch (_config.viewport)
    {
        case Viewport::Fullscreen: return { 0, 0, _terminal.columns(), _terminal.rows() };
        case Viewport::Inline: {
            // For inline mode, the viewport area is the maximum area available for rendering.
            // We use inlineMaxHeight if set, otherwise the full terminal height.
            // The actual content height (_previousContentHeight) is only used for flush positioning.
            int maxHeight = _config.inlineMaxHeight > 0 ? _config.inlineMaxHeight : _terminal.rows();
            return { 0, 0, _terminal.columns(), maxHeight };
        }
        case Viewport::Fixed: return _config.fixedArea;
    }
    return { 0, 0, _terminal.columns(), _terminal.rows() };
}

EventResult Screen::dispatchEvent(InputEvent const& event)
{
    // Handle resize events specially
    if (std::holds_alternative<ResizeEvent>(event))
    {
        auto const& resize = std::get<ResizeEvent>(event);
        _current.resize(resize.rows, resize.columns);
        _previous.resize(resize.rows, resize.columns);
        invalidate();
        return EventResult::Handled;
    }

    // Handle mouse events with hit testing
    if (auto const* mouse = std::get_if<MouseEvent>(&event))
    {
        return dispatchMouseEvent(*mouse);
    }

    // Handle keyboard events - send to focused component
    return dispatchKeyEvent(event);
}

void Screen::setActiveGroup(FocusGroupId const& group)
{
    _activeGroup = group;
}

void Screen::setFocus(Component* component)
{
    if (component)
        setFocus(component->focusGroup(), component);
    else
        setFocus(_activeGroup, nullptr);
}

void Screen::setFocus(FocusGroupId const& group, Component* component)
{
    Component* oldFocus = _focusedComponents[group];
    if (oldFocus != component)
    {
        _focusedComponents[group] = component;
        if (group == _activeGroup)
            updateFocus(oldFocus, component);
    }
}

Component* Screen::focusedComponent() const noexcept
{
    return focusedComponent(_activeGroup);
}

Component* Screen::focusedComponent(FocusGroupId const& group) const noexcept
{
    auto it = _focusedComponents.find(group);
    return it != _focusedComponents.end() ? it->second : nullptr;
}

void Screen::focusNext()
{
    auto focusable = collectFocusableComponents(_activeGroup);
    if (focusable.empty())
        return;

    Component* current = focusedComponent();
    if (!current)
    {
        setFocus(focusable.front());
        return;
    }

    auto it = std::find(focusable.begin(), focusable.end(), current);
    if (it == focusable.end() || ++it == focusable.end())
        setFocus(focusable.front()); // Wrap around
    else
        setFocus(*it);
}

void Screen::focusPrev()
{
    auto focusable = collectFocusableComponents(_activeGroup);
    if (focusable.empty())
        return;

    Component* current = focusedComponent();
    if (!current)
    {
        setFocus(focusable.back());
        return;
    }

    auto it = std::find(focusable.begin(), focusable.end(), current);
    if (it == focusable.end() || it == focusable.begin())
        setFocus(focusable.back()); // Wrap around
    else
        setFocus(*--it);
}

// --- Private Methods ---

void Screen::beginFrame()
{
    // Resize buffers if terminal size changed
    auto const termRows = _terminal.rows();
    auto const termCols = _terminal.columns();

    if (_current.rows() != termRows || _current.cols() != termCols)
    {
        _current.resize(termRows, termCols);
        _previous.resize(termRows, termCols);
        _needsFullRedraw = true;
    }

    // Clear current buffer
    _current.clear(_theme.textNormal);
}

void Screen::renderTree()
{
    // Calculate root bounds based on viewport
    Rect rootBounds = viewportArea();

    // Set root's screen bounds
    _root->setScreenBounds(rootBounds);

    // Render root's children sorted by z-index
    std::vector<Component*> sortedChildren(_root->children().begin(), _root->children().end());
    std::sort(sortedChildren.begin(), sortedChildren.end(), [](Component* a, Component* b) {
        return a->zIndex() < b->zIndex();
    });

    for (Component* child: sortedChildren)
    {
        if (child->visible())
            renderComponent(*child, rootBounds);
    }
}

void Screen::renderComponent(Component& component, Rect parentBounds)
{
    // Calculate component's screen bounds
    Rect localArea = component.area();
    Rect screenBounds = localArea.offset(parentBounds.x, parentBounds.y);

    // Clip to parent bounds
    screenBounds = screenBounds.intersect(parentBounds);
    if (screenBounds.empty())
        return;

    component.setScreenBounds(screenBounds);

    // Create canvas for this component
    Canvas canvas(_current, screenBounds, _theme);

    // Render the component
    component.render(canvas);

    // Render children sorted by z-index
    std::vector<Component*> sortedChildren(component.children().begin(), component.children().end());
    std::sort(sortedChildren.begin(), sortedChildren.end(), [](Component* a, Component* b) {
        return a->zIndex() < b->zIndex();
    });

    for (Component* child: sortedChildren)
    {
        if (child->visible())
            renderComponent(*child, screenBounds);
    }
}

void Screen::renderOverlays()
{
    // Render overlays on top of the main tree, without clipping to parent bounds
    for (auto const& entry: _overlays)
    {
        if (!entry.component || !entry.component->visible())
            continue;

        // Calculate overlay bounds at absolute position
        Size overlaySize = entry.component->preferredSize();
        Rect overlayBounds { entry.position.x, entry.position.y, overlaySize.width, overlaySize.height };

        // Clip only to screen bounds, not to any parent
        Rect screenBounds = viewportArea();
        overlayBounds = overlayBounds.intersect(screenBounds);
        if (overlayBounds.empty())
            continue;

        entry.component->setScreenBounds(overlayBounds);

        // Create canvas for overlay (clipped to screen only)
        Canvas canvas(_current, overlayBounds, _theme);
        entry.component->render(canvas);

        // Render overlay's children (if any)
        for (Component* child: entry.component->children())
        {
            if (child->visible())
                renderComponent(*child, overlayBounds);
        }
    }
}

void Screen::showOverlay(Component& overlay, Point position)
{
    // Check if already visible
    auto it = std::find_if(
        _overlays.begin(), _overlays.end(), [&](auto const& e) { return e.component == &overlay; });

    if (it != _overlays.end())
    {
        // Update position
        it->position = position;
    }
    else
    {
        // Add new overlay
        _overlays.push_back({ &overlay, position });
        overlay.setScreen(this);
    }
    invalidate();
}

void Screen::hideOverlay(Component& overlay)
{
    auto it = std::find_if(
        _overlays.begin(), _overlays.end(), [&](auto const& e) { return e.component == &overlay; });

    if (it != _overlays.end())
    {
        it->component->setScreen(nullptr);
        _overlays.erase(it);
        invalidate();
    }
}

void Screen::positionOverlay(Component& overlay, Point position)
{
    auto it = std::find_if(
        _overlays.begin(), _overlays.end(), [&](auto const& e) { return e.component == &overlay; });

    if (it != _overlays.end())
    {
        it->position = position;
        invalidate();
    }
}

bool Screen::isOverlayVisible(Component const& overlay) const noexcept
{
    return std::find_if(
               _overlays.begin(), _overlays.end(), [&](auto const& e) { return e.component == &overlay; })
           != _overlays.end();
}

void Screen::endFrame()
{
    // Swap buffers for next frame
    // Note: we swap after flush, so _previous holds the last rendered state
}

void Screen::flush()
{
    // Use synchronized output to prevent tearing
    auto syncGuard = _terminal.output().syncGuard();

    switch (_config.viewport)
    {
        case Viewport::Fullscreen: flushFullscreen(); break;
        case Viewport::Inline: flushInline(); break;
        case Viewport::Fixed: flushFixed(); break;
    }

    // Apply cursor shape based on focused component
    applyCursorShape();

    // Swap buffers
    std::swap(_current, _previous);
    _needsFullRedraw = false;
    _dirtyComponents.clear();
}

void Screen::flushFullscreen()
{
    auto& out = _terminal.output();
    bool useDiff = (_renderMode == RenderMode::Diff) && !_needsFullRedraw;

    // Hide cursor during update
    out.hideCursor();

    for (int row = 0; row < _current.rows(); ++row)
    {
        for (int col = 0; col < _current.cols();)
        {
            Cell const& cell = _current.at(row, col);

            // Skip continuation cells
            if (cell.isContinuation())
            {
                ++col;
                continue;
            }

            // Check if cell changed (when using diff)
            bool needsUpdate = !useDiff;
            if (useDiff && _previous.inBounds(row, col))
            {
                Cell const& prevCell = _previous.at(row, col);
                needsUpdate = (cell != prevCell);
            }

            if (needsUpdate)
            {
                out.moveTo(row + 1, col + 1); // Terminal is 1-based
                out.write(cell.grapheme, cell.style);
            }

            col += std::max(1, static_cast<int>(cell.width));
        }
    }

    // Restore cursor
    if (_current.cursorVisible())
    {
        Point cursor = _current.cursor();
        out.moveTo(cursor.y + 1, cursor.x + 1);
        out.showCursor();
    }

    out.flush();
}

void Screen::flushInline()
{
    auto& out = _terminal.output();

    // Calculate content height (find last non-empty row)
    int contentHeight = 0;
    for (int row = _current.rows() - 1; row >= 0; --row)
    {
        bool hasContent = false;
        for (int col = 0; col < _current.cols(); ++col)
        {
            Cell const& cell = _current.at(row, col);
            if (!cell.grapheme.empty() && cell.grapheme != " ")
            {
                hasContent = true;
                break;
            }
        }
        if (hasContent)
        {
            contentHeight = row + 1;
            break;
        }
    }

    // Move cursor to start of content region (row 0)
    // The cursor is currently at _previousCursorRow from the last render
    if (_previousContentHeight > 0)
    {
        // Move from previous cursor position to row 0
        if (_previousCursorRow > 0)
            out.moveUp(_previousCursorRow);
    }

    // If content grew beyond our peak, we need to emit newlines to make room
    // Only emit newlines when growing BEYOND the peak, not when re-showing an overlay
    // that we already made room for.
    if (contentHeight > _peakContentHeight)
    {
        if (_peakContentHeight == 0)
        {
            // First render - query cursor position to know where we're starting
            auto const [cursorRow, cursorCol] = _terminal.queryCursorPosition();
            if (cursorRow > 0)
            {
                // cursorRow is 1-based, convert to 0-based
                _inlineContentStartRow = cursorRow - 1;
            }
            else
            {
                // Query failed, assume bottom of terminal
                _inlineContentStartRow = _terminal.rows() - contentHeight;
            }
            _peakContentHeight = contentHeight;
        }
        else
        {
            // Content grew - emit newlines to make room
            int newLines = contentHeight - _peakContentHeight;
            for (int i = 0; i < newLines; ++i)
                out.writeRaw("\n");

            // Move cursor back to top of our region
            if (newLines > 0)
                out.moveUp(newLines);

            _peakContentHeight = contentHeight;
            _totalNewlinesEmitted += newLines;

            // Adjust start row for emitted newlines
            _inlineContentStartRow -= newLines;
        }
    }
    else if (_inlineContentStartRow < 0)
    {
        // Haven't set start row yet but peak is already set (shouldn't happen, but handle it)
        _inlineContentStartRow = _terminal.rows() - _mainContentHeight - _totalNewlinesEmitted;
    }

    // Track how many rows to clear if content shrank
    int rowsToClear = (_previousContentHeight > contentHeight && contentHeight > 0)
                          ? (_previousContentHeight - contentHeight)
                          : 0;

    _previousContentHeight = contentHeight;

    // Now render each line
    bool useDiff = (_renderMode == RenderMode::Diff) && !_needsFullRedraw;

    out.hideCursor();

    for (int row = 0; row < contentHeight; ++row)
    {
        out.writeRaw("\r"); // Move to start of line

        for (int col = 0; col < _current.cols();)
        {
            Cell const& cell = _current.at(row, col);

            if (cell.isContinuation())
            {
                ++col;
                continue;
            }

            bool needsUpdate = !useDiff;
            if (useDiff && _previous.inBounds(row, col))
            {
                Cell const& prevCell = _previous.at(row, col);
                needsUpdate = (cell != prevCell);
            }

            if (needsUpdate)
            {
                // For inline mode, we render sequentially
                out.write(cell.grapheme, cell.style);
            }
            else
            {
                // Move cursor past unchanged cells
                out.moveRight(cell.width > 0 ? cell.width : 1);
            }

            col += std::max(1, static_cast<int>(cell.width));
        }

        out.clearToEndOfLine();

        if (row < contentHeight - 1)
            out.writeRaw("\n");
    }

    // Clear excess rows when content shrank (to avoid leaving garbage on screen)
    if (rowsToClear > 0)
    {
        // We're currently at row contentHeight-1, need to clear rows below
        for (int i = 0; i < rowsToClear; ++i)
        {
            out.writeRaw("\n");
            out.writeRaw("\r");
            out.clearToEndOfLine();
        }
        // Move back up to where we were (row contentHeight-1)
        out.moveUp(rowsToClear);
    }

    // Position cursor and track its row for next render
    Point cursor = _current.cursor();
    _previousCursorRow = cursor.y;

    if (_current.cursorVisible())
    {
        // Move to cursor position (we're currently at row contentHeight-1)
        int rowsToMoveUp = contentHeight - 1 - cursor.y;
        if (rowsToMoveUp > 0)
            out.moveUp(rowsToMoveUp);
        else if (rowsToMoveUp < 0)
            out.moveDown(-rowsToMoveUp);

        out.writeRaw("\r");
        if (cursor.x > 0)
            out.moveRight(cursor.x);

        out.showCursor();
    }

    out.flush();
}

void Screen::flushFixed()
{
    // Similar to fullscreen but only within the fixed area
    auto& out = _terminal.output();
    Rect area = _config.fixedArea;
    bool useDiff = (_renderMode == RenderMode::Diff) && !_needsFullRedraw;

    out.hideCursor();

    for (int row = area.y; row < area.bottom() && row < _current.rows(); ++row)
    {
        for (int col = area.x; col < area.right() && col < _current.cols();)
        {
            Cell const& cell = _current.at(row, col);

            if (cell.isContinuation())
            {
                ++col;
                continue;
            }

            bool needsUpdate = !useDiff;
            if (useDiff && _previous.inBounds(row, col))
            {
                Cell const& prevCell = _previous.at(row, col);
                needsUpdate = (cell != prevCell);
            }

            if (needsUpdate)
            {
                out.moveTo(row + 1, col + 1);
                out.write(cell.grapheme, cell.style);
            }

            col += std::max(1, static_cast<int>(cell.width));
        }
    }

    if (_current.cursorVisible())
    {
        Point cursor = _current.cursor();
        out.moveTo(cursor.y + 1, cursor.x + 1);
        out.showCursor();
    }

    out.flush();
}

void Screen::applyCursorShape()
{
    CursorShape desiredShape = CursorShape::Default;
    if (Component* focused = focusedComponent())
        desiredShape = focused->cursorShape();

    if (desiredShape != _currentCursorShape)
    {
        _terminal.output().setCursorShape(desiredShape);
        _currentCursorShape = desiredShape;
    }
}

Component* Screen::componentAt(int row, int col) const
{
    return componentAtRecursive(*_root, row, col);
}

Component* Screen::componentAtRecursive(Component& component, int row, int col) const
{
    // Check children in reverse z-order (highest z-index first)
    std::vector<Component*> sortedChildren(component.children().begin(), component.children().end());
    std::sort(sortedChildren.begin(), sortedChildren.end(), [](Component* a, Component* b) {
        return a->zIndex() > b->zIndex();
    });

    for (Component* child: sortedChildren)
    {
        if (child->visible() && child->screenBounds().contains(col, row))
        {
            // Recursively check this child's children
            if (Component* found = componentAtRecursive(*child, row, col))
                return found;
            return child;
        }
    }

    // No child contains the point
    if (component.screenBounds().contains(col, row))
        return &component;

    return nullptr;
}

EventResult Screen::bubbleEvent(Component* target, InputEvent const& event)
{
    while (target)
    {
        EventResult result = target->onEvent(event);
        if (result != EventResult::Ignored)
            return result;
        target = target->parent();
    }
    return EventResult::Ignored;
}

EventResult Screen::dispatchKeyEvent(InputEvent const& event)
{
    Component* focused = focusedComponent();
    if (!focused)
        return EventResult::Ignored;

    return bubbleEvent(focused, event);
}

EventResult Screen::dispatchMouseEvent(MouseEvent const& mouse)
{
    // Note: We intentionally ignore mouse.uiHandled to ensure hover detection
    // works even when the terminal (e.g., Contour with passive tracking) has
    // already processed the event for its own UI purposes.

    int mouseRow = mouse.y - 1; // Convert to 0-based
    int mouseCol = mouse.x - 1;

    // Translate mouse coordinates for inline mode
    // Use _mainContentHeight (content before overlays) to avoid tooltip affecting coordinates
    int const contentHeight = (_mainContentHeight > 0) ? _mainContentHeight : _previousContentHeight;
    if (_config.viewport == Viewport::Inline && contentHeight > 0)
    {
        // _inlineContentStartRow is calculated in flushInline() based on terminal size and peak content
        // height
        if (_inlineContentStartRow < 0)
        {
            // Not yet rendered - skip mouse handling
            if (mouse.type == MouseEvent::Type::Move)
                _hoverState.onMouseMove(mouseCol + 1, 0, nullptr);
            return EventResult::Ignored;
        }

        mouseRow = mouse.y - 1 - _inlineContentStartRow;

        // If mouse is above the inline content area, ignore
        if (mouseRow < 0 || mouseRow >= contentHeight)
        {
            // Still update hover state (to trigger leave if needed)
            if (mouse.type == MouseEvent::Type::Move)
                _hoverState.onMouseMove(mouseCol + 1, mouseRow + 1, nullptr);
            return EventResult::Ignored;
        }
    }

    // Hit test to find target component
    Component* target = componentAt(mouseRow, mouseCol);

    // Update hover state for mouse move events
    // Use viewport-relative 1-based coordinates for consistency with component bounds
    if (mouse.type == MouseEvent::Type::Move)
    {
        _hoverState.onMouseMove(mouseCol + 1, mouseRow + 1, target);
    }

    if (!target)
        return EventResult::Ignored;

    // Create adjusted event with component-relative coordinates
    MouseEvent adjusted = mouse;
    Rect bounds = target->screenBounds();
    adjusted.x = mouseCol - bounds.x + 1; // Back to 1-based for component
    adjusted.y = mouseRow - bounds.y + 1;

    return bubbleEvent(target, adjusted);
}

int Screen::pollTimeoutMs() const
{
    return _hoverState.timeoutMs();
}

void Screen::tickHover()
{
    _hoverState.tick(std::chrono::steady_clock::now());
}

void Screen::showTooltip(std::string_view text, Point position, TooltipContentType contentType)
{
    _tooltip.setContent(text, contentType);

    // Calculate tooltip size and adjust position to fit on screen
    auto const tooltipSize = _tooltip.preferredSize();
    auto const screenArea = viewportArea();

    // Adjust X to keep tooltip on screen
    int x = position.x;
    if (x + tooltipSize.width > screenArea.width)
        x = std::max(0, screenArea.width - tooltipSize.width);

    // Prefer showing below, but show above if not enough room
    int y = position.y + 1; // Below cursor
    if (y + tooltipSize.height > screenArea.height)
    {
        // Try above
        y = position.y - tooltipSize.height;
        if (y < 0)
            y = 0; // Clamp to top
    }

    showOverlay(_tooltip, Point { x, y });
    _tooltipVisible = true;
}

void Screen::hideTooltip()
{
    if (_tooltipVisible)
    {
        hideOverlay(_tooltip);
        _tooltipVisible = false;
    }
}

void Screen::updateFocus(Component* oldFocus, Component* newFocus)
{
    if (oldFocus)
        oldFocus->setFocused(false);
    if (newFocus)
        newFocus->setFocused(true);
}

std::vector<Component*> Screen::collectFocusableComponents(FocusGroupId const& group) const
{
    std::vector<Component*> result;
    collectFocusableRecursive(*_root, group, result);
    return result;
}

void Screen::collectFocusableRecursive(Component& component,
                                       FocusGroupId const& group,
                                       std::vector<Component*>& out) const
{
    if (component.focusable() && component.visible() && component.focusGroup() == group)
        out.push_back(&component);

    for (Component* child: component.children())
        collectFocusableRecursive(*child, group, out);
}

} // namespace tui
