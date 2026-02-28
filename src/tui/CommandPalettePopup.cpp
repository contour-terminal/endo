// SPDX-License-Identifier: Apache-2.0
#include "CommandPalettePopup.hpp"

#include <algorithm>
#include <ranges>

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
#include <tui/Theme.hpp>
#include <tui/Unicode.hpp>
#include <tui/completer/FuzzyMatch.hpp>

namespace tui
{

namespace
{
    /// @brief Calculates display width of a UTF-8 string.
    auto stringWidth(std::string_view text) -> int
    {
        auto width = 0;
        auto segmenter = unicode::utf8_grapheme_segmenter(text);
        for (auto it = segmenter.begin(); it != segmenter.end(); ++it)
            width += graphemeClusterWidth(*it);
        return width;
    }

    /// @brief Renders text with highlighted match positions grapheme-by-grapheme.
    /// @return Number of columns consumed.
    auto putStringWithHighlights(Canvas& canvas,
                                 int row,
                                 int col,
                                 std::string_view text,
                                 Style const& normalStyle,
                                 Style const& matchStyle,
                                 std::vector<size_t> const& matchPositions) -> int
    {
        auto currentCol = col;
        size_t graphemeIndex = 0;

        auto segmenter = unicode::utf8_grapheme_segmenter(text);
        for (auto it = segmenter.begin(); it != segmenter.end(); ++it, ++graphemeIndex)
        {
            auto const& cluster = *it;

            auto nextIt = it;
            ++nextIt;
            char const* clusterStart = it._clusterStart;
            char const* clusterEnd =
                (nextIt != segmenter.end()) ? nextIt._clusterStart : (text.data() + text.size());
            auto const grapheme =
                std::string_view(clusterStart, static_cast<size_t>(clusterEnd - clusterStart));

            auto const graphemeWidth = graphemeClusterWidth(cluster);

            auto const isMatch = std::ranges::find(matchPositions, graphemeIndex) != matchPositions.end();
            auto const& style = isMatch ? matchStyle : normalStyle;

            canvas.put(row, currentCol, grapheme, style);
            currentCol += graphemeWidth;
        }

        return currentCol - col;
    }
} // namespace

// ============================================================================
// Component Interface
// ============================================================================

void CommandPalettePopup::render(Canvas& canvas)
{
    if (!visible())
        return;

    auto const& theme = canvas.theme();

    auto const visibleCount = std::min(static_cast<size_t>(MaxVisibleItems), _filteredItems.size());
    auto const paletteWidth = calculateWidth(canvas.width());
    // Height: 2 (borders) + 1 (filter) + 1 (separator) + visible items
    auto const paletteHeight = static_cast<int>(visibleCount) + 4;

    _renderedHeight = paletteHeight;
    _renderedWidth = paletteWidth;

    auto const innerWidth = paletteWidth - 2;

    // Draw border
    auto const borderRect = Rect { 0, 0, paletteWidth, paletteHeight };
    canvas.drawBox(borderRect, BorderStyle::Rounded, theme.dialogBorder);

    // Row 1: Filter input field
    auto const filterRow = 1;
    auto const filterBg = theme.completionItem;
    canvas.fill(Rect { 1, filterRow, innerWidth, 1 }, ' ', filterBg);

    // Render prompt indicator
    auto const promptText = _title.empty() ? std::string_view("> ") : std::string_view(_title);
    auto promptStyle = Style {};
    promptStyle.fg = theme.agentColors.leftBar;
    auto promptCol = 1 + canvas.putString(filterRow, 1, promptText, promptStyle);

    // Render filter text
    auto const filterText = _filterField.text();
    if (!filterText.empty())
        canvas.putString(filterRow, promptCol, filterText, filterBg);

    // Row 2: Separator line
    auto const sepRow = 2;
    for (auto col = 1; col < paletteWidth - 1; ++col)
        canvas.put(sepRow, col, "\xe2\x94\x80", theme.dialogBorder); // ─

    // Scroll indicator at top if needed
    if (_scrollOffset > 0)
        canvas.put(sepRow, paletteWidth - 2, "\u25B2", theme.textMuted);

    // Draw filtered items
    for (size_t i = 0; i < visibleCount; ++i)
    {
        auto const itemIndex = _scrollOffset + i;
        auto const& filteredItem = _filteredItems[itemIndex];
        auto const* entry = filteredItem.entry;
        auto const isSelected = (itemIndex == _selected);

        auto const row = static_cast<int>(i) + 3; // After border + filter + separator

        auto const& itemStyle = isSelected ? theme.completionSelected : theme.completionItem;
        auto const& matchStyle = theme.completionMatch;

        // Fill row background
        canvas.fill(Rect { 1, row, innerWidth, 1 }, ' ', itemStyle);

        auto col = 1;

        // Category prefix (dimmed)
        if (!entry->category.empty())
        {
            auto const catText = entry->category + ": ";
            col += canvas.putString(row, col, catText, theme.completionDesc);
        }

        // Label with fuzzy match highlighting
        if (!filteredItem.matchPositions.empty())
        {
            col += putStringWithHighlights(
                canvas, row, col, entry->label, itemStyle, matchStyle, filteredItem.matchPositions);
        }
        else
        {
            col += canvas.putString(row, col, entry->label, itemStyle);
        }

        // Right-aligned: keybinding hint
        if (!entry->keybinding.empty())
        {
            auto const kbWidth = stringWidth(entry->keybinding);
            auto const kbCol = 1 + innerWidth - kbWidth;
            if (kbCol > col + 2)
                canvas.putString(row, kbCol, entry->keybinding, theme.textMuted);
        }
        // Description (between label and keybinding)
        else if (!entry->description.empty())
        {
            auto const descWidth = stringWidth(entry->description);
            auto const descCol = 1 + innerWidth - descWidth;
            if (descCol > col + 2)
                canvas.putString(row, descCol, entry->description, theme.completionDesc);
        }
    }

    // Scroll indicator at bottom if needed
    if (_scrollOffset + visibleCount < _filteredItems.size())
    {
        auto const bottomRow = paletteHeight - 1;
        canvas.put(bottomRow, paletteWidth - 2, "\u25BC", theme.textMuted);
    }
}

EventResult CommandPalettePopup::onEvent(InputEvent const& event)
{
    if (!visible())
        return EventResult::Ignored;

    auto const action = processEvent(event);
    if (action != CommandPaletteAction::Changed)
        return EventResult::Handled;
    invalidate();
    return EventResult::Handled;
}

Size CommandPalettePopup::preferredSize() const
{
    if (!visible())
        return { 0, 0 };

    auto const visibleCount = std::min(static_cast<size_t>(MaxVisibleItems), _filteredItems.size());
    auto const height = static_cast<int>(visibleCount) + 4; // borders + filter + separator
    return { MaxWidth, height };
}

// ============================================================================
// Visibility and Items
// ============================================================================

void CommandPalettePopup::show(CommandRegistry const& registry, CommandContext context)
{
    _allItems.clear();
    _title.clear();

    auto const filtered = registry.commandsForContext(context);
    _allItems.reserve(filtered.size());
    for (auto const* entry: filtered)
        _allItems.push_back(*entry);

    _filterField.clear();
    _filterField.setPrompt("> ");
    _selected = 0;
    _scrollOffset = 0;
    _visible = true;

    refilter();
}

void CommandPalettePopup::showSubMenu(std::vector<CommandEntry> items, std::string_view title)
{
    _allItems = std::move(items);
    _title = std::string(title) + "> ";
    _filterField.clear();
    _selected = 0;
    _scrollOffset = 0;
    _visible = true;

    refilter();
}

void CommandPalettePopup::hide()
{
    _visible = false;
    _allItems.clear();
    _filteredItems.clear();
    _filterField.clear();
    _title.clear();
    _selected = 0;
    _scrollOffset = 0;
}

bool CommandPalettePopup::visible() const noexcept
{
    return _visible;
}

// ============================================================================
// Event Handling
// ============================================================================

CommandPaletteAction CommandPalettePopup::processEvent(InputEvent const& event)
{
    if (!visible())
        return CommandPaletteAction::Dismissed;

    if (auto const* key = std::get_if<KeyEvent>(&event))
        return handleKey(*key);

    // Forward paste events to filter field
    if (std::holds_alternative<PasteEvent>(event))
    {
        auto const action = _filterField.processEvent(event);
        if (action == InputFieldAction::Changed)
        {
            refilter();
            return CommandPaletteAction::Changed;
        }
    }

    return CommandPaletteAction::Changed;
}

CommandPaletteAction CommandPalettePopup::handleKey(KeyEvent const& key)
{
    auto const mods = withoutLockKeys(key.modifiers);

    // Escape: dismiss
    if (key.key == KeyCode::Escape)
    {
        hide();
        return CommandPaletteAction::Dismissed;
    }

    // Enter: execute selected
    if (key.key == KeyCode::Enter && mods == Modifier::None)
    {
        executeSelected();
        return CommandPaletteAction::Executed;
    }

    // Navigation: Up / Ctrl+K
    if (key.key == KeyCode::Up || (key.codepoint == 'k' && hasModifier(mods, Modifier::Ctrl)))
    {
        selectPrev();
        return CommandPaletteAction::Changed;
    }

    // Navigation: Down / Ctrl+J
    if (key.key == KeyCode::Down || (key.codepoint == 'j' && hasModifier(mods, Modifier::Ctrl)))
    {
        selectNext();
        return CommandPaletteAction::Changed;
    }

    // PageUp / PageDown
    if (key.key == KeyCode::PageUp)
    {
        pageUp();
        return CommandPaletteAction::Changed;
    }
    if (key.key == KeyCode::PageDown)
    {
        pageDown();
        return CommandPaletteAction::Changed;
    }

    // All other keys: forward to filter field
    auto const action = _filterField.processEvent(InputEvent { key });
    if (action == InputFieldAction::Changed)
    {
        refilter();
        return CommandPaletteAction::Changed;
    }

    return CommandPaletteAction::Changed;
}

int CommandPalettePopup::renderedHeight() const noexcept
{
    return _renderedHeight;
}

int CommandPalettePopup::renderedWidth() const noexcept
{
    return _renderedWidth;
}

// ============================================================================
// Filtering and Selection
// ============================================================================

void CommandPalettePopup::refilter()
{
    _filteredItems.clear();

    auto const filterText = _filterField.text();

    if (filterText.empty())
    {
        // Show all items when no filter
        for (auto const& item: _allItems)
            _filteredItems.push_back(FilteredItem { .entry = &item, .matchPositions = {}, .score = 0 });
    }
    else
    {
        for (auto const& item: _allItems)
        {
            // Match against label
            auto result = FuzzyMatch::matchSmartCase(item.label, filterText);
            if (result.matches)
            {
                auto const score = FuzzyMatch::calculateScore(100, item.label, filterText, result);
                _filteredItems.push_back(FilteredItem {
                    .entry = &item, .matchPositions = std::move(result.positions), .score = score });
                continue;
            }

            // Also match against category + label
            auto const combined = item.category + " " + item.label;
            result = FuzzyMatch::matchSmartCase(combined, filterText);
            if (result.matches)
            {
                auto const score = FuzzyMatch::calculateScore(50, combined, filterText, result);
                // Adjust positions to skip category prefix for label highlighting
                auto const categoryLen = FuzzyMatch::countGraphemes(item.category) + 1; // +1 for space
                auto labelPositions = std::vector<size_t> {};
                for (auto pos: result.positions)
                {
                    if (pos >= categoryLen)
                        labelPositions.push_back(pos - categoryLen);
                }
                _filteredItems.push_back(FilteredItem {
                    .entry = &item, .matchPositions = std::move(labelPositions), .score = score });
                continue;
            }

            // Also match against description
            result = FuzzyMatch::matchSmartCase(item.description, filterText);
            if (result.matches)
            {
                auto const score = FuzzyMatch::calculateScore(30, item.description, filterText, result);
                _filteredItems.push_back(
                    FilteredItem { .entry = &item, .matchPositions = {}, .score = score });
            }
        }

        // Sort by score descending
        std::ranges::sort(_filteredItems, [](auto const& a, auto const& b) { return a.score > b.score; });
    }

    // Reset selection
    _selected = 0;
    _scrollOffset = 0;
}

void CommandPalettePopup::ensureSelectedVisible()
{
    if (_selected < _scrollOffset)
        _scrollOffset = _selected;
    else if (_selected >= _scrollOffset + MaxVisibleItems)
        _scrollOffset = _selected - MaxVisibleItems + 1;
}

int CommandPalettePopup::calculateWidth(int maxWidth) const
{
    auto width = MinWidth;

    for (auto const& item: _filteredItems)
    {
        auto const* entry = item.entry;
        auto itemWidth = 2; // borders
        if (!entry->category.empty())
            itemWidth += stringWidth(entry->category) + 2; // ": "
        itemWidth += stringWidth(entry->label);
        if (!entry->keybinding.empty())
            itemWidth += 2 + stringWidth(entry->keybinding);
        else if (!entry->description.empty())
            itemWidth += 2 + stringWidth(entry->description);

        width = std::max(width, itemWidth);
    }

    return std::min(std::min(width, MaxWidth), maxWidth);
}

void CommandPalettePopup::executeSelected()
{
    if (_selected < _filteredItems.size())
    {
        // Copy the action before hide() destroys _allItems (which entry points into).
        auto action = _filteredItems[_selected].entry->action;
        hide();
        if (action)
            action();
    }
}

void CommandPalettePopup::selectNext()
{
    if (_filteredItems.empty())
        return;
    _selected = (_selected + 1) % _filteredItems.size();
    ensureSelectedVisible();
}

void CommandPalettePopup::selectPrev()
{
    if (_filteredItems.empty())
        return;
    _selected = (_selected == 0) ? _filteredItems.size() - 1 : _selected - 1;
    ensureSelectedVisible();
}

void CommandPalettePopup::pageDown()
{
    if (_filteredItems.empty())
        return;
    _selected = std::min(_selected + MaxVisibleItems, _filteredItems.size() - 1);
    ensureSelectedVisible();
}

void CommandPalettePopup::pageUp()
{
    if (_filteredItems.empty())
        return;
    _selected = (_selected >= static_cast<size_t>(MaxVisibleItems)) ? _selected - MaxVisibleItems : 0;
    ensureSelectedVisible();
}

} // namespace tui
