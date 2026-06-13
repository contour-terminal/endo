// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tui/CursorShape.hpp>
#include <tui/InputEvent.hpp>
#include <tui/Rect.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace tui
{

class Canvas;
class Screen;

// Forward declaration from Tooltip.hpp
enum class TooltipContentType : std::uint8_t;

/// @brief Result of a hover query on a component.
///
/// Components return this from onHover() to provide tooltip content.
/// Supports plain text and markdown. Multi-line content causes the tooltip
/// to grow dynamically up to the Tooltip's configured max height (default 15),
/// after which scrolling is enabled automatically.
struct HoverResult
{
    std::string text;               ///< Tooltip content (plain text or markdown).
    Point position;                 ///< Component-relative anchor for tooltip placement.
    TooltipContentType contentType; ///< How to render the text.
};

/// Focus group identifier.
using FocusGroupId = std::string;

/// Default focus group name.
inline constexpr std::string_view DefaultFocusGroup = "default";

/// Result of event handling.
enum class EventResult : uint8_t
{
    Ignored,   ///< Event not handled, bubble to parent.
    Handled,   ///< Event handled, stop bubbling.
    FocusNext, ///< Request focus move to next component.
    FocusPrev, ///< Request focus move to previous component.
};

/// Layout parameters for a child component.
struct LayoutParams
{
    Rect area;                                                ///< Position and size relative to parent.
    bool visible = true;                                      ///< Whether to render this component.
    int zIndex = 0;                                           ///< Stacking order (higher = on top).
    FocusGroupId focusGroup = std::string(DefaultFocusGroup); ///< Focus group this component belongs to.
};

/// Base class for all TUI components.
///
/// Components form a tree structure with parent-child relationships.
/// Each component has an area (position and size relative to its parent),
/// visibility state, and focus group membership.
///
/// Subclasses must implement render() to draw themselves using a Canvas.
/// They can optionally override event handling and focus-related methods.
class Component
{
  public:
    virtual ~Component();

    // Prevent copying (component identity matters for tree structure)
    Component(Component const&) = delete;
    Component& operator=(Component const&) = delete;
    Component(Component&&) = delete;
    Component& operator=(Component&&) = delete;

    // --- Rendering ---

    /// Renders this component to the given canvas.
    /// The canvas's area is already set to this component's bounds.
    /// @param canvas The canvas to render to.
    virtual void render(Canvas& canvas) = 0;

    /// Returns the preferred size for this component.
    /// Used by layout algorithms.
    [[nodiscard]] virtual Size preferredSize() const { return { .width = 0, .height = 1 }; }

    /// Returns the minimum size this component can shrink to.
    [[nodiscard]] virtual Size minSize() const { return { .width = 0, .height = 0 }; }

    // --- Event Handling ---

    /// Processes an input event.
    /// Override to handle keyboard, mouse, or other events.
    /// @param event The input event to process.
    /// @return EventResult indicating whether the event was handled.
    [[nodiscard]] virtual EventResult onEvent(InputEvent const& event);

    /// Called when this component gains focus.
    virtual void onFocus() {}

    /// Called when this component loses focus.
    virtual void onBlur() {}

    /// @brief Called when the user hovers over this component after the hover delay.
    ///
    /// Override to provide tooltip content. Supports single-line and multi-line
    /// content in both plain text and markdown formats. Tooltips grow dynamically
    /// in height and enable scrolling when content exceeds the configured maximum.
    ///
    /// @param x Component-relative column (0-based).
    /// @param y Component-relative row (0-based).
    /// @return Tooltip info if hover content available, std::nullopt otherwise.
    [[nodiscard]] virtual std::optional<HoverResult> onHover(int x, int y);

    // --- Focus ---

    /// Returns true if this component can receive keyboard focus.
    [[nodiscard]] virtual bool focusable() const { return false; }

    /// Returns true if this component currently has focus within its group.
    [[nodiscard]] bool focused() const noexcept { return _focused; }

    /// Returns the cursor shape to use when this component is focused.
    /// Default is CursorShape::Default, which lets the terminal decide.
    [[nodiscard]] virtual CursorShape cursorShape() const { return CursorShape::Default; }

    // --- Visibility ---

    /// Sets whether this component is visible.
    /// Invisible components are not rendered and don't receive events.
    void setVisible(bool visible);

    /// Returns true if this component is visible.
    /// Virtual so derived components (e.g. popups) can refine visibility while
    /// remaining correct when invoked through a Component base pointer.
    [[nodiscard]] virtual bool visible() const noexcept { return _layout.visible; }

    // --- Layout ---

    /// Sets the area (position and size) of this component relative to its parent.
    void setArea(Rect area);

    /// Returns the area of this component relative to its parent.
    [[nodiscard]] Rect area() const noexcept { return _layout.area; }

    /// Returns the absolute screen bounds of this component.
    /// Only valid after the component has been rendered.
    [[nodiscard]] Rect screenBounds() const noexcept { return _screenBounds; }

    /// Sets the absolute screen bounds of this component.
    /// Used by Screen during rendering, and for off-screen component rendering.
    void setScreenBounds(Rect bounds);

    /// Sets the z-index (stacking order) of this component.
    void setZIndex(int z);

    /// Returns the z-index of this component.
    [[nodiscard]] int zIndex() const noexcept { return _layout.zIndex; }

    // --- Hierarchy ---

    /// Adds a child component with the given layout parameters.
    /// The child's parent is set to this component.
    /// @param child The child component to add.
    /// @param params Layout parameters for the child.
    void addChild(Component& child, LayoutParams params = {});

    /// Removes a child component.
    /// The child's parent is set to nullptr.
    /// @param child The child component to remove.
    void removeChild(Component& child);

    /// Removes all child components.
    void clearChildren();

    /// Returns the children of this component.
    [[nodiscard]] std::span<Component* const> children() const noexcept;

    /// Returns the parent of this component, or nullptr if none.
    [[nodiscard]] Component* parent() const noexcept { return _parent; }

    // --- Focus Group ---

    /// Sets the focus group this component belongs to.
    void setFocusGroup(FocusGroupId group);

    /// Returns the focus group this component belongs to.
    [[nodiscard]] FocusGroupId const& focusGroup() const noexcept { return _layout.focusGroup; }

    // --- Screen Access ---

    /// Returns the screen this component is attached to, or nullptr.
    [[nodiscard]] Screen* screen() const noexcept { return _screen; }

  protected:
    Component() = default;

    /// Marks this component as needing a redraw.
    void invalidate();

  private:
    friend class Screen;

    Component* _parent = nullptr;
    std::vector<Component*> _children;
    LayoutParams _layout;
    Rect _screenBounds; ///< Absolute screen position (computed during render)
    Screen* _screen = nullptr;
    bool _focused = false;

    /// Called by Screen to set the parent.
    void setParent(Component* parent);

    /// Called by Screen to attach/detach from the screen.
    void setScreen(Screen* screen);

    /// Called by Screen to update focus state.
    void setFocused(bool focused);
};

/// Implicit root component used by Screen.
/// This is a simple container that renders all its children.
class RootComponent final: public Component
{
  public:
    void render(Canvas& canvas) override;
    [[nodiscard]] Size preferredSize() const override;
};

} // namespace tui
