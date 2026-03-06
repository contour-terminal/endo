// SPDX-License-Identifier: Apache-2.0
#include "Component.hpp"

#include <tui/Canvas.hpp>
#include <tui/Screen.hpp>

#include <algorithm>
#include <utility>

namespace tui
{

Component::~Component()
{
    // Remove from parent if attached
    if (_parent)
        _parent->removeChild(*this);

    // Clear children (sets their parent to nullptr)
    clearChildren();
}

EventResult Component::onEvent([[maybe_unused]] InputEvent const& event)
{
    return EventResult::Ignored;
}

std::optional<HoverResult> Component::onHover(int /*x*/, int /*y*/)
{
    return std::nullopt;
}

void Component::setVisible(bool visible)
{
    if (_layout.visible != visible)
    {
        _layout.visible = visible;
        invalidate();
    }
}

void Component::setArea(Rect area)
{
    if (_layout.area != area)
    {
        _layout.area = area;
        invalidate();
    }
}

void Component::setZIndex(int z)
{
    if (_layout.zIndex != z)
    {
        _layout.zIndex = z;
        invalidate();
    }
}

void Component::addChild(Component& child, LayoutParams params)
{
    // Remove from previous parent if any
    if (child._parent)
        child._parent->removeChild(child);

    // Set layout parameters
    child._layout = std::move(params);

    // Add to our children
    _children.push_back(&child);
    child.setParent(this);

    // Propagate screen attachment
    if (_screen)
        child.setScreen(_screen);

    invalidate();
}

void Component::removeChild(Component& child)
{
    auto it = std::ranges::find(_children, &child);
    if (it != _children.end())
    {
        _children.erase(it);
        child.setParent(nullptr);
        child.setScreen(nullptr);
        invalidate();
    }
}

void Component::clearChildren()
{
    for (Component* child: _children)
    {
        child->setParent(nullptr);
        child->setScreen(nullptr);
    }
    _children.clear();
    invalidate();
}

std::span<Component* const> Component::children() const noexcept
{
    return _children;
}

void Component::setFocusGroup(FocusGroupId group)
{
    _layout.focusGroup = std::move(group);
}

void Component::invalidate()
{
    if (_screen)
        _screen->invalidate(*this);
}

void Component::setParent(Component* parent)
{
    _parent = parent;
}

void Component::setScreen(Screen* screen)
{
    _screen = screen;

    // Propagate to children
    for (Component* child: _children)
        child->setScreen(screen);
}

void Component::setFocused(bool focused)
{
    if (_focused != focused)
    {
        _focused = focused;
        if (focused)
            onFocus();
        else
            onBlur();
    }
}

void Component::setScreenBounds(Rect bounds)
{
    _screenBounds = bounds;
}

// --- RootComponent ---

void RootComponent::render([[maybe_unused]] Canvas& canvas)
{
    // RootComponent doesn't render anything itself.
    // Children are rendered by Screen::renderTree().
}

Size RootComponent::preferredSize() const
{
    // Root takes full screen, so preferred size is not meaningful.
    return { .width = 0, .height = 0 };
}

} // namespace tui
