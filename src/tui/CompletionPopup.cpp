// SPDX-License-Identifier: Apache-2.0
#include "CompletionPopup.hpp"

#include <algorithm>

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wold-style-cast"
#endif
#include <libunicode/utf8_grapheme_segmenter.h>
#include <libunicode/width.h>
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

#include <tui/Canvas.hpp>
#include <tui/Theme.hpp>

namespace tui
{

namespace
{
    /// @brief Calculates display width of a UTF-8 string.
    auto stringWidth(std::string_view text) -> int
    {
        int width = 0;
        auto segmenter = unicode::utf8_grapheme_segmenter(text);
        for (auto it = segmenter.begin(); it != segmenter.end(); ++it)
        {
            auto const& cluster = *it;
            for (char32_t cp: cluster)
                width += unicode::width(cp);
        }
        return width;
    }

    /// @brief Truncates a string to fit within a given width, adding ellipsis.
    /// @return A pair of (truncated string, display width).
    auto truncateWithEllipsis(std::string_view text, int maxWidth) -> std::pair<std::string, int>
    {
        std::string result;
        int cols = 0;
        auto segmenter = unicode::utf8_grapheme_segmenter(text);

        for (auto it = segmenter.begin(); it != segmenter.end(); ++it)
        {
            auto const& cluster = *it;

            // Calculate cluster width
            int clusterWidth = 0;
            for (char32_t cp: cluster)
                clusterWidth += unicode::width(cp);
            if (clusterWidth == 0)
                clusterWidth = 1;

            // Check if adding this cluster plus "..." would exceed max
            if (cols + clusterWidth + 3 > maxWidth)
                break;

            // Get the cluster as string_view
            auto nextIt = it;
            ++nextIt;
            char const* clusterStart = it._clusterStart;
            char const* clusterEnd =
                (nextIt != segmenter.end()) ? nextIt._clusterStart : (text.data() + text.size());
            result.append(clusterStart, clusterEnd);
            cols += clusterWidth;
        }

        result += "...";
        return { result, cols + 3 };
    }
} // namespace

// ============================================================================
// Component Interface
// ============================================================================

void CompletionPopup::render(Canvas& canvas)
{
    if (!visible())
        return;

    auto const& theme = canvas.theme();

    // Calculate dimensions
    size_t visibleCount = std::min(static_cast<size_t>(_maxVisible), _items.size());
    int menuWidth = calculateWidth(canvas.width());
    int menuHeight = static_cast<int>(visibleCount) + 2; // +2 for border

    _renderedHeight = menuHeight;
    _renderedWidth = menuWidth;

    int innerWidth = menuWidth - 2; // Subtract borders

    // Draw border using Canvas API
    auto borderRect = Rect { 0, 0, menuWidth, menuHeight };
    canvas.drawBox(borderRect, BorderStyle::Single, theme.dialogBorder);

    // Scroll indicator at top if needed
    if (_scrollOffset > 0)
    {
        canvas.put(0, menuWidth - 2, "\u25B2", theme.textMuted); // U+25B2: Black Up-Pointing Triangle
    }

    // Draw items
    for (size_t i = 0; i < visibleCount; ++i)
    {
        size_t itemIndex = _scrollOffset + i;
        auto const& item = _items[itemIndex];
        bool isSelected = (itemIndex == _selected);

        int row = static_cast<int>(i) + 1; // +1 for top border

        // Choose style based on selection
        auto const& itemStyle = isSelected ? theme.completionSelected : theme.completionItem;

        // Get display text
        std::string_view displayText = item.displayText.empty() ? item.text : item.displayText;

        // Calculate available space for text and description
        int textWidth = stringWidth(displayText);
        int descWidth = 0;
        std::string_view desc;

        if (!item.description.empty())
        {
            desc = item.description;
            descWidth = stringWidth(desc);
        }

        // Fill the row background (x=col, y=row, width, height)
        canvas.fill(Rect { 1, row, innerWidth, 1 }, ' ', itemStyle);

        // Calculate total content width
        int totalContentWidth = textWidth + (descWidth > 0 ? 2 + descWidth : 0);

        if (totalContentWidth <= innerWidth)
        {
            // Everything fits
            canvas.putString(row, 1, displayText, itemStyle);

            // Draw description right-aligned
            if (descWidth > 0)
            {
                int descCol = 1 + innerWidth - descWidth;
                canvas.putString(row, descCol, desc, theme.completionDesc);
            }
        }
        else
        {
            // Need to truncate
            int maxTextWidth = innerWidth - (descWidth > 0 ? 2 + std::min(10, descWidth) : 0);
            if (maxTextWidth < 5)
                maxTextWidth = innerWidth;

            if (textWidth > maxTextWidth)
            {
                // Truncate display text with ellipsis
                auto [truncated, truncWidth] = truncateWithEllipsis(displayText, maxTextWidth);
                canvas.putString(row, 1, truncated, itemStyle);
                textWidth = truncWidth;
            }
            else
            {
                canvas.putString(row, 1, displayText, itemStyle);
            }

            // Draw description if there's room
            int remaining = innerWidth - textWidth;
            if (descWidth > 0 && remaining > 5)
            {
                int availableDesc = remaining - 2;
                if (descWidth > availableDesc)
                {
                    // Truncate description
                    auto [truncDesc, truncDescWidth] = truncateWithEllipsis(desc, availableDesc - 1);
                    // Replace "..." with unicode ellipsis for descriptions
                    if (truncDesc.size() >= 3 && truncDesc.substr(truncDesc.size() - 3) == "...")
                    {
                        truncDesc.erase(truncDesc.size() - 3);
                        truncDesc += "\u2026";
                        truncDescWidth -= 2; // Ellipsis is 1 column instead of 3
                    }
                    int descCol = 1 + innerWidth - truncDescWidth;
                    canvas.putString(row, descCol, truncDesc, theme.completionDesc);
                }
                else
                {
                    int descCol = 1 + innerWidth - descWidth;
                    canvas.putString(row, descCol, desc, theme.completionDesc);
                }
            }
        }
    }

    // Scroll indicator at bottom if needed
    if (_scrollOffset + visibleCount < _items.size())
    {
        canvas.put(menuHeight - 1,
                   menuWidth - 2,
                   "\u25BC",
                   theme.textMuted); // U+25BC: Black Down-Pointing Triangle
    }
}

EventResult CompletionPopup::onEvent(InputEvent const& event)
{
    auto action = processEvent(event);

    switch (action)
    {
        case CompletionAction::Changed:
        case CompletionAction::Accepted: invalidate(); return EventResult::Handled;
        case CompletionAction::Dismissed:
            invalidate();
            return EventResult::Ignored; // Let parent handle the key that dismissed us
    }
    return EventResult::Ignored;
}

Size CompletionPopup::preferredSize() const
{
    if (_items.empty())
        return { 0, 0 };

    size_t visibleCount = std::min(static_cast<size_t>(_maxVisible), _items.size());
    int width = calculateWidth(200);                 // Use large max for preferred size
    int height = static_cast<int>(visibleCount) + 2; // +2 for border

    return { width, height };
}

// ============================================================================
// Visibility and Items
// ============================================================================

void CompletionPopup::show(std::vector<CompletionItem> items)
{
    _items = std::move(items);
    _selected = 0;
    _scrollOffset = 0;
    _visible = !_items.empty();
    Component::setVisible(_visible); // Sync Component visibility state
}

void CompletionPopup::hide()
{
    _items.clear();
    _selected = 0;
    _scrollOffset = 0;
    _visible = false;
    _renderedHeight = 0;
    _renderedWidth = 0;
    Component::setVisible(false); // Sync Component visibility state
}

void CompletionPopup::updateItems(std::vector<CompletionItem> items)
{
    if (items.empty())
    {
        hide();
        return;
    }

    // Remember current selection text
    std::string previousSelection;
    if (_selected < _items.size())
        previousSelection = _items[_selected].text;

    _items = std::move(items);
    _scrollOffset = 0;

    // Try to find the previously selected item in the new list
    if (!previousSelection.empty())
    {
        for (size_t i = 0; i < _items.size(); ++i)
        {
            if (_items[i].text == previousSelection)
            {
                _selected = i;
                ensureSelectedVisible();
                _visible = true;
                Component::setVisible(true);
                return;
            }
        }
    }

    // Not found - select first item (best match)
    _selected = 0;
    _visible = true;
    Component::setVisible(true);
}

bool CompletionPopup::visible() const noexcept
{
    return _visible && !_items.empty();
}

size_t CompletionPopup::itemCount() const noexcept
{
    return _items.size();
}

bool CompletionPopup::empty() const noexcept
{
    return _items.empty();
}

// ============================================================================
// Selection
// ============================================================================

size_t CompletionPopup::selectedIndex() const noexcept
{
    return _selected;
}

CompletionItem const* CompletionPopup::selectedItem() const noexcept
{
    if (_items.empty())
        return nullptr;
    return &_items[_selected];
}

CompletionItem const* CompletionPopup::itemAt(size_t index) const noexcept
{
    if (index >= _items.size())
        return nullptr;
    return &_items[index];
}

void CompletionPopup::selectNext()
{
    if (_items.empty())
        return;

    if (_selected + 1 < _items.size())
        ++_selected;
    else
        _selected = 0; // Wrap around

    ensureSelectedVisible();
}

void CompletionPopup::selectPrev()
{
    if (_items.empty())
        return;

    if (_selected > 0)
        --_selected;
    else
        _selected = _items.size() - 1; // Wrap around

    ensureSelectedVisible();
}

void CompletionPopup::pageDown()
{
    if (_items.empty())
        return;

    size_t pageSize = static_cast<size_t>(_maxVisible);
    if (_selected + pageSize < _items.size())
        _selected += pageSize;
    else
        _selected = _items.size() - 1;

    ensureSelectedVisible();
}

void CompletionPopup::pageUp()
{
    if (_items.empty())
        return;

    size_t pageSize = static_cast<size_t>(_maxVisible);
    if (_selected > pageSize)
        _selected -= pageSize;
    else
        _selected = 0;

    ensureSelectedVisible();
}

void CompletionPopup::selectFirst()
{
    _selected = 0;
    ensureSelectedVisible();
}

void CompletionPopup::selectLast()
{
    if (!_items.empty())
        _selected = _items.size() - 1;
    ensureSelectedVisible();
}

void CompletionPopup::ensureSelectedVisible()
{
    if (_items.empty())
        return;

    size_t visibleCount = std::min(static_cast<size_t>(_maxVisible), _items.size());

    if (_selected < _scrollOffset)
        _scrollOffset = _selected;
    else if (_selected >= _scrollOffset + visibleCount)
        _scrollOffset = _selected - visibleCount + 1;
}

// ============================================================================
// Configuration
// ============================================================================

void CompletionPopup::setMaxVisible(int maxVisible)
{
    _maxVisible = std::max(1, maxVisible);
}

int CompletionPopup::maxVisible() const noexcept
{
    return _maxVisible;
}

// ============================================================================
// Event Handling
// ============================================================================

CompletionAction CompletionPopup::processEvent(InputEvent const& event)
{
    if (!visible())
        return CompletionAction::Dismissed;

    if (auto const* key = std::get_if<KeyEvent>(&event))
        return handleKey(*key);

    return CompletionAction::Dismissed;
}

CompletionAction CompletionPopup::handleKey(KeyEvent const& key)
{
    // Down arrow or Ctrl+J (vim-style)
    if (key.key == KeyCode::Down || (key.codepoint == 'j' && hasModifier(key.modifiers, Modifier::Ctrl)))
    {
        selectNext();
        return CompletionAction::Changed;
    }

    // Up arrow or Ctrl+K (vim-style)
    if (key.key == KeyCode::Up || (key.codepoint == 'k' && hasModifier(key.modifiers, Modifier::Ctrl)))
    {
        selectPrev();
        return CompletionAction::Changed;
    }

    if (key.key == KeyCode::PageDown)
    {
        pageDown();
        return CompletionAction::Changed;
    }

    if (key.key == KeyCode::PageUp)
    {
        pageUp();
        return CompletionAction::Changed;
    }

    if (key.key == KeyCode::Home)
    {
        selectFirst();
        return CompletionAction::Changed;
    }

    if (key.key == KeyCode::End)
    {
        selectLast();
        return CompletionAction::Changed;
    }

    // Tab: auto-accept if only one item, otherwise cycle to next/previous
    if (key.key == KeyCode::Tab)
    {
        if (_items.size() == 1)
            return CompletionAction::Accepted; // Auto-accept single remaining item

        if (hasModifier(key.modifiers, Modifier::Shift))
            selectPrev();
        else
            selectNext();
        return CompletionAction::Changed;
    }

    // Accept selection with Enter
    if (key.key == KeyCode::Enter)
    {
        return CompletionAction::Accepted;
    }

    // Dismiss popup on Escape or any other unhandled key
    // Any unhandled key dismisses the popup, allowing the key to be processed by parent
    return CompletionAction::Dismissed;
}

// ============================================================================
// Rendering
// ============================================================================

int CompletionPopup::calculateWidth(int maxWidth) const
{
    if (_items.empty())
        return 0;

    int maxItemWidth = 0;
    for (auto const& item: _items)
    {
        int itemWidth =
            static_cast<int>(item.displayText.empty() ? item.text.size() : item.displayText.size());

        // Add description width if present
        if (!item.description.empty())
            itemWidth += 2 + static_cast<int>(item.description.size()); // "  " separator + description

        maxItemWidth = std::max(maxItemWidth, itemWidth);
    }

    // Add 4 for border (2) + padding (2)
    int width = maxItemWidth + 4;
    return std::min(width, maxWidth);
}

int CompletionPopup::renderedHeight() const noexcept
{
    return _renderedHeight;
}

int CompletionPopup::renderedWidth() const noexcept
{
    return _renderedWidth;
}

} // namespace tui
