// SPDX-License-Identifier: Apache-2.0
#include "CompletionPopup.hpp"

#include <algorithm>

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wold-style-cast"
#endif
#include <libunicode/utf8_grapheme_segmenter.h>
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

#include <tui/Box.hpp>
#include <tui/Canvas.hpp>
#include <tui/PopupKeyDispatch.hpp>
#include <tui/Theme.hpp>
#include <tui/Unicode.hpp>

namespace tui
{

namespace
{
    constexpr int detailMaxWidth = 40; ///< Maximum detail panel width.
    constexpr int detailMinWidth = 20; ///< Minimum detail panel width.

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
            int const clusterWidth = graphemeClusterWidth(cluster);

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
    size_t visibleCount = std::min(_selection.maxVisible(), _items.size());
    int menuWidth = calculateWidth(canvas.width());
    int menuHeight = static_cast<int>(visibleCount) + 2; // +2 for border

    _renderedHeight = menuHeight;
    _renderedWidth = menuWidth;

    int innerWidth = menuWidth - 2; // Subtract borders

    // Draw border using Canvas API
    auto borderRect = Rect { .x = 0, .y = 0, .width = menuWidth, .height = menuHeight };
    canvas.drawBox(borderRect, BorderStyle::Single, theme.dialogBorder);

    // Scroll indicator at top if needed
    if (_selection.scrollOffset() > 0)
    {
        canvas.put(0, menuWidth - 2, "\u25B2", theme.textMuted);
    }

    // Draw items
    for (size_t i = 0; i < visibleCount; ++i)
    {
        size_t itemIndex = _selection.scrollOffset() + i;
        auto const& item = _items[itemIndex];
        bool isSelected = (itemIndex == _selection.selected());

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
        canvas.fill(Rect { .x = 1, .y = row, .width = innerWidth, .height = 1 }, ' ', itemStyle);

        // Calculate total content width
        int totalContentWidth = textWidth + (descWidth > 0 ? 2 + descWidth : 0);

        // Determine if we need fuzzy match highlighting
        bool hasHighlights = !item.matchPositions.empty();
        Style const& matchStyle = theme.completionMatch;

        if (totalContentWidth <= innerWidth)
        {
            // Everything fits
            if (hasHighlights)
                putStringWithHighlights(
                    canvas, row, 1, displayText, itemStyle, matchStyle, item.matchPositions);
            else
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
                // Note: For truncated text, highlighting is disabled as positions may be invalid
                auto [truncated, truncWidth] = truncateWithEllipsis(displayText, maxTextWidth);
                canvas.putString(row, 1, truncated, itemStyle);
                textWidth = truncWidth;
            }
            else
            {
                if (hasHighlights)
                    putStringWithHighlights(
                        canvas, row, 1, displayText, itemStyle, matchStyle, item.matchPositions);
                else
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
    if (_selection.scrollOffset() + visibleCount < _items.size())
    {
        canvas.put(menuHeight - 1, menuWidth - 2, "\u25BC", theme.textMuted);
    }

    // ========================================================================
    // Detail panel (side documentation popup)
    // ========================================================================
    auto const detailWidth = detailPanelWidth();
    auto const availableForDetail = canvas.width() - menuWidth;

    if (!_detailContent.empty() && detailWidth > 0 && availableForDetail >= detailMinWidth)
    {
        auto const panelWidth = std::min(detailWidth, availableForDetail);
        auto const border = BorderChars::fromStyle(BorderStyle::Single);

        // Replace main popup's right border with T-junctions for seamless merge
        canvas.put(0, menuWidth - 1, border.topT, theme.dialogBorder);
        canvas.put(menuHeight - 1, menuWidth - 1, border.bottomT, theme.dialogBorder);

        // Draw detail panel top border (from shared border to right edge)
        for (int col = menuWidth; col < menuWidth + panelWidth - 1; ++col)
            canvas.put(0, col, border.horizontal, theme.dialogBorder);
        canvas.put(0, menuWidth + panelWidth - 1, border.topRight, theme.dialogBorder);

        // Draw detail panel bottom border
        for (int col = menuWidth; col < menuWidth + panelWidth - 1; ++col)
            canvas.put(menuHeight - 1, col, border.horizontal, theme.dialogBorder);
        canvas.put(menuHeight - 1, menuWidth + panelWidth - 1, border.bottomRight, theme.dialogBorder);

        // Draw right border
        for (int row = 1; row < menuHeight - 1; ++row)
            canvas.put(row, menuWidth + panelWidth - 1, border.vertical, theme.dialogBorder);

        // Fill detail panel interior with background
        auto const detailInnerWidth = panelWidth - 2; // minus shared border and right border
        for (int row = 1; row < menuHeight - 1; ++row)
            canvas.fill(Rect { .x = menuWidth, .y = row, .width = detailInnerWidth, .height = 1 },
                        ' ',
                        theme.completionDetail);

        // Render styled detail content into a subcanvas
        auto const detailInnerHeight = menuHeight - 2;
        auto detailCanvas = canvas.subcanvas(
            Rect { .x = menuWidth, .y = 1, .width = detailInnerWidth, .height = detailInnerHeight });
        _detailContent.renderTo(detailCanvas, _detailScrollOffset, detailInnerHeight);

        _renderedWidth = menuWidth + panelWidth;
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
        return { .width = 0, .height = 0 };

    auto const visibleCount = std::min(_selection.maxVisible(), _items.size());
    auto width = calculateWidth(200);                       // Use large max for preferred size
    auto const height = static_cast<int>(visibleCount) + 2; // +2 for border

    // Add detail panel width (shared border means -1)
    auto const dpWidth = detailPanelWidth();
    if (dpWidth > 0)
        width += dpWidth - 1;

    return { .width = width, .height = height };
}

// ============================================================================
// Visibility and Items
// ============================================================================

void CompletionPopup::show(std::vector<CompletionItem> items)
{
    _items = std::move(items);
    _selection.reset();
    _visible = !_items.empty();
    Component::setVisible(_visible);
    updateDetailContent();
}

void CompletionPopup::hide()
{
    _items.clear();
    _selection.reset();
    _visible = false;
    _renderedHeight = 0;
    _renderedWidth = 0;
    _detailContent.clear();
    _detailScrollOffset = 0;
    Component::setVisible(false);
}

void CompletionPopup::updateItems(std::vector<CompletionItem> items)
{
    if (items.empty())
    {
        hide();
        return;
    }

    std::string previousSelection;
    if (_selection.selected() < _items.size())
        previousSelection = _items[_selection.selected()].text;

    _items = std::move(items);
    _selection.reset();

    if (!previousSelection.empty())
    {
        for (size_t i = 0; i < _items.size(); ++i)
        {
            if (_items[i].text == previousSelection)
            {
                _selection.select(i, _items.size());
                _visible = true;
                Component::setVisible(true);
                updateDetailContent();
                return;
            }
        }
    }

    _visible = true;
    Component::setVisible(true);
    updateDetailContent();
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

std::vector<CompletionItem> const& CompletionPopup::items() const noexcept
{
    return _items;
}

// ============================================================================
// Selection
// ============================================================================

size_t CompletionPopup::selectedIndex() const noexcept
{
    return _selection.selected();
}

CompletionItem const* CompletionPopup::selectedItem() const noexcept
{
    if (_items.empty())
        return nullptr;
    return &_items[_selection.selected()];
}

CompletionItem const* CompletionPopup::itemAt(size_t index) const noexcept
{
    if (index >= _items.size())
        return nullptr;
    return &_items[index];
}

void CompletionPopup::selectNext()
{
    _selection.selectNext(_items.size());
    updateDetailContent();
}

void CompletionPopup::selectPrev()
{
    _selection.selectPrev(_items.size());
    updateDetailContent();
}

void CompletionPopup::pageDown()
{
    _selection.pageDown(_items.size());
    updateDetailContent();
}

void CompletionPopup::pageUp()
{
    _selection.pageUp(_items.size());
    updateDetailContent();
}

void CompletionPopup::selectFirst()
{
    _selection.selectFirst(_items.size());
    updateDetailContent();
}

void CompletionPopup::selectLast()
{
    _selection.selectLast(_items.size());
    updateDetailContent();
}

// ============================================================================
// Configuration
// ============================================================================

void CompletionPopup::setMaxVisible(int maxVisible)
{
    _selection.setMaxVisible(static_cast<size_t>(std::max(1, maxVisible)));
}

int CompletionPopup::maxVisible() const noexcept
{
    return static_cast<int>(_selection.maxVisible());
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
    // Navigation via shared key dispatch
    auto const nav = classifyNavigationKey(key);
    if (nav != PopupNavAction::None)
    {
        switch (nav)
        {
            case PopupNavAction::SelectNext: selectNext(); break;
            case PopupNavAction::SelectPrev: selectPrev(); break;
            case PopupNavAction::PageDown: pageDown(); break;
            case PopupNavAction::PageUp: pageUp(); break;
            case PopupNavAction::SelectFirst: selectFirst(); break;
            case PopupNavAction::SelectLast: selectLast(); break;
            case PopupNavAction::None: break;
        }
        return CompletionAction::Changed;
    }

    // Tab: auto-accept if only one item, otherwise cycle to next/previous
    if (key.key == KeyCode::Tab)
    {
        if (_items.size() == 1)
            return CompletionAction::Accepted;

        if (hasModifier(key.modifiers, Modifier::Shift))
            selectPrev();
        else
            selectNext();
        return CompletionAction::Changed;
    }

    if (key.key == KeyCode::Enter)
        return CompletionAction::Accepted;

    // Modifier-only keys should not dismiss the popup
    if (isModifierOnlyKey(key.key))
        return CompletionAction::Changed;

    // Any unhandled key dismisses the popup
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

void CompletionPopup::updateDetailContent()
{
    _detailScrollOffset = 0;

    if (_items.empty() || _selection.selected() >= _items.size())
    {
        _detailContent.clear();
        return;
    }

    auto const& detail = _items[_selection.selected()].detail;
    if (detail.empty())
    {
        _detailContent.clear();
        return;
    }

    // Parse markdown with width clamped to detail panel max
    _detailContent = StyledText::fromMarkdown(detail, detailMaxWidth - 2);
}

int CompletionPopup::detailPanelWidth() const
{
    if (_detailContent.empty())
        return 0;

    // Content width + 2 (shared left border + right border)
    auto const contentWidth = std::min(_detailContent.maxLineWidth(), detailMaxWidth - 2);
    return std::clamp(contentWidth + 2, detailMinWidth, detailMaxWidth);
}

} // namespace tui
