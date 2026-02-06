// SPDX-License-Identifier: Apache-2.0
#include "Screen.hpp"

#include <algorithm>

#include <tui/Canvas.hpp>
#include <tui/Terminal.hpp>

namespace tui
{

Screen::Screen(Terminal& terminal, ScreenConfig config):
    _terminal(terminal), _config(config), _theme(darkTheme()), _root(std::make_unique<RootComponent>())
{
    _root->setScreen(this);

    // Initialize buffers based on terminal size
    auto const termRows = _terminal.rows();
    auto const termCols = _terminal.columns();
    _current.resize(termRows, termCols);
    _previous.resize(termRows, termCols);
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

void Screen::endFrame()
{
    // Swap buffers for next frame
    // Note: we swap after flush, so _previous holds the last rendered state
}

void Screen::flush()
{
    switch (_config.viewport)
    {
        case Viewport::Fullscreen: flushFullscreen(); break;
        case Viewport::Inline: flushInline(); break;
        case Viewport::Fixed: flushFixed(); break;
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

    // If content grew, scroll terminal by emitting newlines
    if (contentHeight > _previousContentHeight)
    {
        int newLines = contentHeight - _previousContentHeight;
        for (int i = 0; i < newLines; ++i)
            out.writeRaw("\n");

        // Move cursor back to top of our region
        if (contentHeight > 0)
            out.moveUp(contentHeight);
    }

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
    // Skip if terminal UI handled it
    if (mouse.uiHandled)
        return EventResult::Ignored;

    // Hit test to find target component
    // Mouse coordinates are 1-based, convert to 0-based
    Component* target = componentAt(mouse.y - 1, mouse.x - 1);
    if (!target)
        return EventResult::Ignored;

    // Create adjusted event with component-relative coordinates
    MouseEvent adjusted = mouse;
    Rect bounds = target->screenBounds();
    adjusted.x = mouse.x - bounds.x;
    adjusted.y = mouse.y - bounds.y;

    return bubbleEvent(target, adjusted);
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
