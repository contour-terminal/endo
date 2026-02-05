// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <cctype>

#include <tui/List.hpp>

namespace tui
{

namespace
{

    auto toLower(std::string_view str) -> std::string
    {
        auto result = std::string {};
        result.reserve(str.size());
        for (auto ch: str)
            result += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return result;
    }

    auto containsIgnoreCase(std::string_view haystack, std::string_view needle) -> bool
    {
        if (needle.empty())
            return true;

        auto const lowerHaystack = toLower(haystack);
        auto const lowerNeedle = toLower(needle);
        return lowerHaystack.find(lowerNeedle) != std::string::npos;
    }

} // namespace

List::List(std::vector<ListItem> items): _items(std::move(items))
{
    _style = defaultListStyle();
    rebuildVisibleIndices();
}

void List::setItems(std::vector<ListItem> items)
{
    _items = std::move(items);
    _selectedVisibleIndex = 0;
    _scrollOffset = 0;
    rebuildVisibleIndices();
}

auto List::items() const noexcept -> std::vector<ListItem> const&
{
    return _items;
}

auto List::size() const noexcept -> std::size_t
{
    return _items.size();
}

auto List::empty() const noexcept -> bool
{
    return _items.empty();
}

auto List::selectedIndex() const noexcept -> std::size_t
{
    if (_visibleIndices.empty() || _selectedVisibleIndex >= _visibleIndices.size())
        return 0;
    return _visibleIndices[_selectedVisibleIndex];
}

auto List::selectedItem() const noexcept -> std::optional<ListItem const*>
{
    if (_visibleIndices.empty() || _selectedVisibleIndex >= _visibleIndices.size())
        return std::nullopt;
    return &_items[_visibleIndices[_selectedVisibleIndex]];
}

void List::setSelectedIndex(std::size_t index)
{
    // Find the visible index corresponding to the item index
    for (auto i = std::size_t { 0 }; i < _visibleIndices.size(); ++i)
    {
        if (_visibleIndices[i] == index)
        {
            _selectedVisibleIndex = i;
            return;
        }
    }
}

void List::setFilter(std::string_view filter)
{
    if (_filter == filter)
        return;

    _filter = filter;
    rebuildVisibleIndices();
}

auto List::filter() const noexcept -> std::string_view
{
    return _filter;
}

void List::clearFilter()
{
    setFilter("");
}

auto List::visibleItems() const noexcept -> std::vector<std::size_t> const&
{
    return _visibleIndices;
}

auto List::processEvent(InputEvent const& event) -> ListAction
{
    if (auto const* key = std::get_if<KeyEvent>(&event))
        return handleKey(*key);

    return ListAction::None;
}

auto List::handleKey(KeyEvent const& key) -> ListAction
{
    // Handle vim-style navigation (j/k) for printable keys
    if (isPrintable(key.key) && key.modifiers == Modifier::None)
    {
        if (key.codepoint == 'j')
        {
            selectNext();
            return ListAction::Changed;
        }
        if (key.codepoint == 'k')
        {
            selectPrevious();
            return ListAction::Changed;
        }
    }

    switch (key.key)
    {
        case KeyCode::Up: selectPrevious(); return ListAction::Changed;

        case KeyCode::Down: selectNext(); return ListAction::Changed;

        case KeyCode::Home: selectFirst(); return ListAction::Changed;

        case KeyCode::End: selectLast(); return ListAction::Changed;

        case KeyCode::PageUp: pageUp(10); return ListAction::Changed;

        case KeyCode::PageDown: pageDown(10); return ListAction::Changed;

        case KeyCode::Enter:
            if (!_visibleIndices.empty())
            {
                auto const idx = selectedIndex();
                if (_items[idx].enabled)
                    return ListAction::Selected;
            }
            break;

        case KeyCode::Escape: return ListAction::Cancelled;

        default: break;
    }

    return ListAction::None;
}

void List::render(TerminalOutput& output, int startRow, int startCol, int width, int maxRows) const
{
    if (_visibleIndices.empty())
    {
        output.moveTo(startRow, startCol);
        auto emptyStyle = _style.disabled;
        output.write("  (no items)", emptyStyle);
        return;
    }

    ensureSelectionVisible(maxRows);

    auto const visibleCount =
        std::min(static_cast<std::size_t>(maxRows), _visibleIndices.size() - _scrollOffset);

    for (auto i = std::size_t { 0 }; i < static_cast<std::size_t>(maxRows); ++i)
    {
        auto const row = startRow + static_cast<int>(i);
        output.moveTo(row, startCol);

        if (i >= visibleCount)
        {
            // Clear remaining rows
            output.writeRaw(std::string(static_cast<std::size_t>(width), ' '));
            continue;
        }

        auto const visibleIdx = _scrollOffset + i;
        auto const itemIdx = _visibleIndices[visibleIdx];
        auto const& item = _items[itemIdx];
        auto const isSelected = (visibleIdx == _selectedVisibleIndex);

        // Cursor indicator
        if (isSelected)
            output.write(_style.cursor, _style.selected);
        else
            output.writeRaw(_style.noCursor);

        // Item label
        auto const cursorWidth = static_cast<int>(_style.cursor.size() > 2 ? 2 : _style.cursor.size());
        auto const labelMaxWidth = width - cursorWidth;

        auto const& itemStyle = [&]() -> Style const& {
            if (!item.enabled)
                return _style.disabled;
            return isSelected ? _style.selected : _style.normal;
        }();

        auto displayLabel = item.label;
        if (static_cast<int>(displayLabel.size()) > labelMaxWidth)
            displayLabel = displayLabel.substr(0, static_cast<std::size_t>(labelMaxWidth - 1)) + "\u2026";

        output.write(displayLabel, itemStyle);

        // Padding
        auto const labelLen = static_cast<int>(displayLabel.size());
        auto const padding = std::max(0, labelMaxWidth - labelLen);
        if (padding > 0)
            output.writeRaw(std::string(static_cast<std::size_t>(padding), ' '));
    }

    // Show scroll indicator if needed
    if (_visibleIndices.size() > static_cast<std::size_t>(maxRows))
    {
        auto const hasMore = (_scrollOffset + static_cast<std::size_t>(maxRows) < _visibleIndices.size());
        auto const hasLess = (_scrollOffset > 0);

        if (hasLess)
        {
            output.moveTo(startRow, startCol + width - 2);
            output.write("\u25B2", _style.disabled); // ▲
        }

        if (hasMore)
        {
            output.moveTo(startRow + maxRows - 1, startCol + width - 2);
            output.write("\u25BC", _style.disabled); // ▼
        }
    }
}

void List::setStyle(ListStyle style)
{
    _style = std::move(style);
}

auto List::style() const noexcept -> ListStyle const&
{
    return _style;
}

void List::selectNext()
{
    if (_visibleIndices.empty())
        return;

    // Find next enabled item
    auto start = _selectedVisibleIndex;
    do
    {
        if (_selectedVisibleIndex < _visibleIndices.size() - 1)
            ++_selectedVisibleIndex;
        else
            break; // Don't wrap

        auto const idx = _visibleIndices[_selectedVisibleIndex];
        if (_items[idx].enabled)
            return;
    } while (_selectedVisibleIndex != start);
}

void List::selectPrevious()
{
    if (_visibleIndices.empty())
        return;

    auto start = _selectedVisibleIndex;
    do
    {
        if (_selectedVisibleIndex > 0)
            --_selectedVisibleIndex;
        else
            break; // Don't wrap

        auto const idx = _visibleIndices[_selectedVisibleIndex];
        if (_items[idx].enabled)
            return;
    } while (_selectedVisibleIndex != start);
}

void List::selectFirst()
{
    for (auto i = std::size_t { 0 }; i < _visibleIndices.size(); ++i)
    {
        if (_items[_visibleIndices[i]].enabled)
        {
            _selectedVisibleIndex = i;
            _scrollOffset = 0;
            return;
        }
    }
}

void List::selectLast()
{
    for (auto i = _visibleIndices.size(); i > 0; --i)
    {
        if (_items[_visibleIndices[i - 1]].enabled)
        {
            _selectedVisibleIndex = i - 1;
            return;
        }
    }
}

void List::pageDown(int pageSize)
{
    for (auto i = 0; i < pageSize; ++i)
        selectNext();
}

void List::pageUp(int pageSize)
{
    for (auto i = 0; i < pageSize; ++i)
        selectPrevious();
}

void List::rebuildVisibleIndices()
{
    _visibleIndices.clear();

    for (auto i = std::size_t { 0 }; i < _items.size(); ++i)
    {
        if (matchesFilter(_items[i]))
            _visibleIndices.push_back(i);
    }

    // Reset selection to first enabled item
    _selectedVisibleIndex = 0;
    _scrollOffset = 0;

    for (auto i = std::size_t { 0 }; i < _visibleIndices.size(); ++i)
    {
        if (_items[_visibleIndices[i]].enabled)
        {
            _selectedVisibleIndex = i;
            break;
        }
    }
}

void List::ensureSelectionVisible(int maxRows) const
{
    if (_visibleIndices.empty())
        return;

    if (_selectedVisibleIndex < _scrollOffset)
        _scrollOffset = _selectedVisibleIndex;
    else if (_selectedVisibleIndex >= _scrollOffset + static_cast<std::size_t>(maxRows))
        _scrollOffset = _selectedVisibleIndex - static_cast<std::size_t>(maxRows) + 1;
}

auto List::matchesFilter(ListItem const& item) const -> bool
{
    if (_filter.empty())
        return true;

    auto const& text = item.filterText.empty() ? item.label : item.filterText;
    return containsIgnoreCase(text, _filter);
}

auto defaultListStyle() -> ListStyle
{
    auto style = ListStyle {};

    // Normal items: default color
    style.normal = Style {};

    // Selected items: inverse or bold with accent color
    style.selected = Style {};
    style.selected.bold = true;
    style.selected.fg = RgbColor { .r = 130, .g = 180, .b = 255 }; // Light blue

    // Disabled items: dim
    style.disabled = Style {};
    style.disabled.dim = true;

    // Description: dim/gray
    style.description = Style {};
    style.description.dim = true;

    return style;
}

} // namespace tui
