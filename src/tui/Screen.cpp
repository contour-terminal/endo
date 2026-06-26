// SPDX-License-Identifier: Apache-2.0
#include "Screen.hpp"

#include <tui/Canvas.hpp>
#include <tui/Terminal.hpp>

#include <algorithm>
#include <chrono>
#include <ranges>
#include <unordered_map>

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
    _terminal(terminal), _config(config), _theme(currentTheme()), _root(std::make_unique<RootComponent>())
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

    // Wire hover callbacks: translate hover position to component-relative coordinates
    // and call the component's onHover() virtual method.
    _hoverState.setOnHoverConfirmed([this](HoverInfo const& hover) {
        auto* target = hover.target;
        // Fall back to focused component when hit-testing fails (e.g., in inline mode
        // before first render or after releaseCursor() resets _inlineContentStartRow).
        if (!target)
            target = focusedComponent();
        if (!target)
            return;
        auto const bounds = target->screenBounds();
        int const relX = hover.x - 1 - bounds.x;
        int const relY = hover.y - 1 - bounds.y;
        if (auto result = target->onHover(relX, relY))
        {
            auto const absPos =
                Point { .x = bounds.x + result->position.x, .y = bounds.y + result->position.y };
            showTooltip(result->text, absPos, result->contentType);
        }
    });

    _hoverState.setOnHoverLeave([this]() { hideTooltip(); });

    // Switch to the alternate screen buffer (DEC private mode 1049) so the app paints on a
    // fresh screen and the user's scrollback is preserved; restored in the destructor.
    if (_config.alternateScreen)
    {
        _terminal.output().writeRaw("\033[?1049h");
        _terminal.output().flush();
        _enteredAlternateScreen = true;
    }
}

Screen::~Screen()
{
    if (_enteredAlternateScreen)
    {
        auto& out = _terminal.output();
        out.showCursor();
        out.writeRaw("\033[?1049l"); // leave the alternate screen, restoring the primary buffer
        out.flush();
    }
}

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
        for (auto const row: std::views::iota(0, _current.rows()) | std::views::reverse)
        {
            bool hasContent = false;
            for (auto const col: std::views::iota(0, _current.cols()))
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

void Screen::clearAndRelease()
{
    auto& out = _terminal.output();

    // Move up from the tracked cursor row to the top of the content region.
    if (_previousCursorRow > 0)
        out.moveUp(_previousCursorRow);

    // Clear from here to end of display and flush.
    out.carriageReturn();
    out.clearToEndOfDisplay();
    out.enableReflow();
    out.saveCursor(); // DECSC: anchor exact cursor position for next Screen
    out.flush();

    // Communicate to the next Screen that room already exists on the terminal,
    // so its first flushInline() render can skip the LF room reservation that
    // would otherwise cause +1 cursor drift per shell→agent transition.
    out.setInlineRoomReserved(_peakContentHeight);

    releaseCursor();
}

void Screen::setTheme(Theme theme)
{
    _theme = theme;
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
    return { .width = cols(), .height = rows() };
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
        case Viewport::Fullscreen:
            return { .x = 0, .y = 0, .width = _terminal.columns(), .height = _terminal.rows() };
        case Viewport::Inline: {
            // For inline mode, the viewport area is the maximum area available for rendering.
            // We use inlineMaxHeight if set, otherwise the full terminal height.
            // The actual content height (_previousContentHeight) is only used for flush positioning.
            int maxHeight = _config.inlineMaxHeight > 0 ? _config.inlineMaxHeight : _terminal.rows();
            return { .x = 0, .y = 0, .width = _terminal.columns(), .height = maxHeight };
        }
        case Viewport::Fixed: return _config.fixedArea;
    }
    return { .x = 0, .y = 0, .width = _terminal.columns(), .height = _terminal.rows() };
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

    auto it = std::ranges::find(focusable, current);
    if (it != focusable.end())
        ++it;
    if (it == focusable.end())
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

    auto it = std::ranges::find(focusable, current);
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
    std::ranges::sort(sortedChildren, [](Component* a, Component* b) { return a->zIndex() < b->zIndex(); });

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
    std::ranges::sort(sortedChildren, [](Component* a, Component* b) { return a->zIndex() < b->zIndex(); });

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
        Rect overlayBounds { .x = entry.position.x,
                             .y = entry.position.y,
                             .width = overlaySize.width,
                             .height = overlaySize.height };

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
    auto it = std::ranges::find_if(_overlays, [&](auto const& e) { return e.component == &overlay; });

    if (it != _overlays.end())
    {
        // Update position
        it->position = position;
    }
    else
    {
        // Add new overlay
        _overlays.push_back({ .component = &overlay, .position = position });
        overlay.setScreen(this);
    }
    invalidate();
}

void Screen::hideOverlay(Component& overlay)
{
    auto it = std::ranges::find_if(_overlays, [&](auto const& e) { return e.component == &overlay; });

    if (it != _overlays.end())
    {
        it->component->setScreen(nullptr);
        _overlays.erase(it);
        invalidate();
    }
}

void Screen::positionOverlay(Component& overlay, Point position)
{
    auto it = std::ranges::find_if(_overlays, [&](auto const& e) { return e.component == &overlay; });

    if (it != _overlays.end())
    {
        it->position = position;
        invalidate();
    }
}

bool Screen::isOverlayVisible(Component const& overlay) const noexcept
{
    return std::ranges::find_if(_overlays, [&](auto const& e) { return e.component == &overlay; })
           != _overlays.end();
}

void Screen::endFrame()
{
    // Swap buffers for next frame
    // Note: we swap after flush, so _previous holds the last rendered state
}

void Screen::flush()
{
    if (_config.viewport == Viewport::Inline && _peakContentHeight == 0)
    {
        // First inline render: skip SyncGuard — no previous content to tear against,
        // and avoids SIGCHLD-interrupted write leaving the terminal in sync-buffer mode.
        _terminal.output().flush();
        flushInline();
        applyCursorShape();
        _terminal.output().flush();
    }
    else
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
    }

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

    for (auto const row: std::views::iota(0, _current.rows()))
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
                out.writeText(cell.grapheme, cell.style);
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
    for (auto const row: std::views::iota(0, _current.rows()) | std::views::reverse)
    {
        bool hasContent = false;
        for (auto const col: std::views::iota(0, _current.cols()))
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
            // First render: emit newlines to ensure room at the bottom of the terminal.
            // Without this, rendering near the last row causes \n to scroll the header
            // into scrollback, making it invisible until the next draw.
            //
            // However, if a previous Screen already reserved room via clearAndRelease(),
            // the space already exists on the terminal. Emitting LFs again would push the
            // cursor down by +1 per transition (the Ctrl+T drift bug).
            auto const reservedRoom = out.consumeInlineRoom();
            if (reservedRoom > 0)
            {
                // A previous Screen saved the cursor position — restore it exactly.
                // This eliminates any tracking drift from relative cursor movements.
                out.restoreCursor(); // DECRC: anchor to exact saved position

                // Query cursor position after DECRC to know where content starts.
                auto const [cursorRow1Based, cursorCol1Based] = _terminal.queryCursorPosition();
                auto const startRow = (cursorRow1Based > 0) ? (cursorRow1Based - 1) : (_terminal.rows() - 1);

                if (reservedRoom >= contentHeight)
                {
                    // Room already exists — skip LF emission, just adopt the peak.
                    _peakContentHeight = contentHeight;
                }
                else
                {
                    // Room exists but insufficient — emit additional LFs for the extra rows.
                    auto const newLines = contentHeight - reservedRoom;
                    for ([[maybe_unused]] auto _: std::views::iota(0, newLines))
                        out.linefeed();
                    if (newLines > 0)
                        out.moveUp(newLines);
                    _totalNewlinesEmitted += newLines;
                    _peakContentHeight = contentHeight;
                }

                // Content start: min(cursor row, bottom-aligned position).
                _inlineContentStartRow = std::min(startRow, _terminal.rows() - contentHeight);
            }
            else
            {
                // Very first render (no previous Screen) — reserve room with LFs.
                // Query actual cursor position to compute _inlineContentStartRow accurately.
                auto const [cursorRow1Based, cursorCol1Based] = _terminal.queryCursorPosition();
                auto const startRow = (cursorRow1Based > 0) ? (cursorRow1Based - 1) : (_terminal.rows() - 1);

                // Emit contentHeight-1 newlines (enough to ensure room without overshooting).
                auto const newLines = std::max(0, contentHeight - 1);
                for ([[maybe_unused]] auto _: std::views::iota(0, newLines))
                    out.linefeed();
                if (newLines > 0)
                    out.moveUp(newLines);
                _totalNewlinesEmitted += newLines;
                _peakContentHeight = contentHeight;

                // Content start: min(cursor row, bottom-aligned position).
                // Cursor near top (row 5, rows=50, height=5): start = min(5, 45) = 5
                // Cursor at bottom (row 49, rows=50, height=5): start = min(49, 45) = 45
                _inlineContentStartRow = std::min(startRow, _terminal.rows() - contentHeight);
            }
        }
        else
        {
            // Content grew — emit LFs for the growth and adjust start row only for actual scrolls.
            auto const growth = contentHeight - _peakContentHeight;
            auto const scrollsNeeded = std::max(0, _inlineContentStartRow + contentHeight - _terminal.rows());

            for ([[maybe_unused]] auto _: std::views::iota(0, growth))
                out.linefeed();
            if (growth > 0)
                out.moveUp(growth);

            _peakContentHeight = contentHeight;
            _totalNewlinesEmitted += growth;
            _inlineContentStartRow -= scrollsNeeded;
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

    // Build image row coverage: for each row, the image (if any) that starts there
    auto imageAtRow = std::unordered_map<int, ImageRegion const*> {};
    for (auto const& img: _current.images())
        imageAtRow[img.cellArea.y] = &img;

    // Build previous image lookup for diffing — skip sixel re-emission when unchanged
    auto previousImageAtRow = std::unordered_map<int, ImageRegion const*> {};
    if (useDiff)
    {
        for (auto const& img: _previous.images())
            previousImageAtRow[img.cellArea.y] = &img;
    }

    for (auto row = 0; row < contentHeight; ++row)
    {
        out.carriageReturn(); // Move to start of line

        // Check if an image starts at this row
        if (auto it = imageAtRow.find(row); it != imageAtRow.end())
        {
            auto const& img = *it->second;

            // Only emit sixel when the image actually changed from the previous frame.
            auto needsEmit = !useDiff;
            if (!needsEmit)
            {
                auto const prevIt = previousImageAtRow.find(row);
                needsEmit = (prevIt == previousImageAtRow.end() || prevIt->second->cellArea != img.cellArea
                             || prevIt->second->contentHash != img.contentHash);
            }

            if (needsEmit && !img.encodedSixel.empty())
            {
                // Save cursor before sixel — the terminal may advance the cursor
                // below the image (DECSIXEL mode 80 set), which would corrupt
                // our cursor tracking. DECSC/DECRC restores the exact position.
                out.saveCursor();
                if (img.cellArea.x > 0)
                    out.moveRight(img.cellArea.x);
                out.writeSixel(img.encodedSixel);
                out.restoreCursor();
            }

            // Skip cell rendering for all rows covered by this image.
            // Don't clearToEndOfLine — the sixel covers the visual area and
            // clearing might erase the pixel data in some terminals.
            auto const skipRows = img.cellArea.height - 1;
            for (int s = 0; s < skipRows && row + 1 < contentHeight; ++s)
            {
                out.linefeed();
                ++row;
            }
            if (row < contentHeight - 1)
                out.linefeed();
            continue;
        }

        if (_config.inhibitReflow)
            out.disableReflow();

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
                out.writeText(cell.grapheme, cell.style);
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
            out.linefeed();
    }

    // Clear excess rows when content shrank (to avoid leaving garbage on screen)
    if (rowsToClear > 0)
    {
        // We're currently at row contentHeight-1, need to clear rows below
        for ([[maybe_unused]] auto _: std::views::iota(0, rowsToClear))
        {
            out.linefeed();
            out.carriageReturn();
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

        out.carriageReturn();
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
                out.writeText(cell.grapheme, cell.style);
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
    std::ranges::sort(sortedChildren, [](Component* a, Component* b) { return a->zIndex() > b->zIndex(); });

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
    // Auto-hide tooltip on any key press
    if (_tooltipVisible)
    {
        hideTooltip();
        _hoverState.reset();
    }

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
        y = std::max(y, 0); // Clamp to top
    }

    showOverlay(_tooltip, Point { .x = x, .y = y });
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
