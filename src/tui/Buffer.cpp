// SPDX-License-Identifier: Apache-2.0
#include "Buffer.hpp"

#include <tui/TerminalOutput.hpp>

#include <algorithm>
#include <ranges>
#include <stdexcept>

#include "Unicode.hpp"

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wold-style-cast"
#endif
#include <libunicode/utf8_grapheme_segmenter.h>
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

namespace tui
{

Buffer::Buffer(int rows, int cols): _rows(rows), _cols(cols)
{
    if (_rows > 0 && _cols > 0)
    {
        _cells.resize(static_cast<size_t>(_rows) * static_cast<size_t>(_cols));
        clear();
    }
}

void Buffer::resize(int rows, int cols)
{
    if (rows == _rows && cols == _cols)
        return;

    if (rows <= 0 || cols <= 0)
    {
        _cells.clear();
        _rows = 0;
        _cols = 0;
        _cursor = { .x = 0, .y = 0 };
        return;
    }

    std::vector<Cell> newCells(static_cast<size_t>(rows) * static_cast<size_t>(cols));

    // Initialize all cells to spaces
    for (auto& cell: newCells)
        cell.reset();

    // Copy existing content
    int copyRows = std::min(_rows, rows);
    int copyCols = std::min(_cols, cols);

    for (auto const r: std::views::iota(0, copyRows))
    {
        for (auto const c: std::views::iota(0, copyCols))
        {
            auto const oldIdx =
                (static_cast<size_t>(r) * static_cast<size_t>(_cols)) + static_cast<size_t>(c);
            auto const newIdx = (static_cast<size_t>(r) * static_cast<size_t>(cols)) + static_cast<size_t>(c);
            newCells[newIdx] = _cells[oldIdx];
        }
    }

    _cells = std::move(newCells);
    _rows = rows;
    _cols = cols;
    clearImages();

    // Clamp cursor to new bounds
    _cursor.x = std::min(_cursor.x, _cols - 1);
    _cursor.y = std::min(_cursor.y, _rows - 1);
    _cursor.x = std::max(_cursor.x, 0);
    _cursor.y = std::max(_cursor.y, 0);
}

Cell& Buffer::at(int row, int col)
{
    if (!inBounds(row, col))
        throw std::out_of_range("Buffer::at: coordinates out of bounds");
    return _cells[index(row, col)];
}

Cell const& Buffer::at(int row, int col) const
{
    if (!inBounds(row, col))
        throw std::out_of_range("Buffer::at: coordinates out of bounds");
    return _cells[index(row, col)];
}

Cell* Buffer::tryAt(int row, int col) noexcept
{
    if (!inBounds(row, col))
        return nullptr;
    return &_cells[index(row, col)];
}

Cell const* Buffer::tryAt(int row, int col) const noexcept
{
    if (!inBounds(row, col))
        return nullptr;
    return &_cells[index(row, col)];
}

void Buffer::clear(Style const& style)
{
    for (auto& cell: _cells)
        cell.reset(style);
    clearImages();
}

void Buffer::clearRect(Rect area, Style const& style)
{
    // Clamp area to buffer bounds
    int startRow = std::max(0, area.y);
    int startCol = std::max(0, area.x);
    int endRow = std::min(_rows, area.bottom());
    int endCol = std::min(_cols, area.right());

    for (auto const row: std::views::iota(startRow, endRow))
    {
        for (auto const col: std::views::iota(startCol, endCol))
        {
            _cells[index(row, col)].reset(style);
        }
    }
}

int Buffer::putString(int row, int col, std::string_view text, Style const& style, int maxWidth)
{
    if (row < 0 || row >= _rows || col < 0)
        return 0;

    // Effective right boundary: the lesser of the buffer width and the caller's column budget.
    // Clamp the budget to a non-negative span starting at col, guarding against overflow when
    // maxWidth is the default sentinel (std::numeric_limits<int>::max()).
    int const budgetLimit = (maxWidth >= _cols - col) ? _cols : col + maxWidth;
    int const colLimit = std::min(_cols, budgetLimit);

    int currentCol = col;

    // Use libunicode to segment into grapheme clusters
    auto segmenter = unicode::utf8_grapheme_segmenter(text);

    for (auto it = segmenter.begin(); it != segmenter.end(); ++it)
    {
        if (currentCol >= colLimit)
            break;

        auto const& cluster = *it;

        // Skip C0 control characters (codepoint < 0x20) to prevent terminal corruption.
        if (!cluster.empty() && static_cast<char32_t>(cluster[0]) < 0x20)
            continue;

        // Calculate display width for the grapheme cluster
        int const clusterWidth = graphemeClusterWidth(cluster);

        // Check if it fits within the effective boundary (buffer width and caller budget).
        // A wide cluster that would straddle the boundary is dropped, not partially written,
        // so its continuation cells never spill past colLimit.
        if (currentCol + clusterWidth > colLimit)
            break;

        // Get the cluster as a string_view into the original text
        // it._clusterStart points to the start of the current cluster in the source
        auto nextIt = it;
        ++nextIt;
        char const* clusterStart = it._clusterStart;
        char const* clusterEnd =
            (nextIt != segmenter.end()) ? nextIt._clusterStart : (text.data() + text.size());
        std::string_view clusterView(clusterStart, static_cast<size_t>(clusterEnd - clusterStart));

        // Write the grapheme to the first cell
        Cell& cell = _cells[index(row, currentCol)];
        cell.grapheme = std::string(clusterView);
        cell.style = style;
        cell.width = static_cast<uint8_t>(clusterWidth);

        // Mark continuation cells for wide characters
        for (int i = 1; i < clusterWidth && currentCol + i < _cols; ++i)
        {
            Cell& contCell = _cells[index(row, currentCol + i)];
            contCell.resetContinuation();
            contCell.style = style;
        }

        currentCol += clusterWidth;
    }

    return currentCol - col;
}

void Buffer::fill(Rect area, char ch, Style const& style)
{
    // Clamp area to buffer bounds
    int startRow = std::max(0, area.y);
    int startCol = std::max(0, area.x);
    int endRow = std::min(_rows, area.bottom());
    int endCol = std::min(_cols, area.right());

    std::string grapheme(1, ch);

    for (auto const row: std::views::iota(startRow, endRow))
    {
        for (auto const col: std::views::iota(startCol, endCol))
        {
            Cell& cell = _cells[index(row, col)];
            cell.grapheme = grapheme;
            cell.style = style;
            cell.width = 1;
        }
    }
}

void Buffer::setCursor(int row, int col) noexcept
{
    _cursor.x = col;
    _cursor.y = row;
}

void Buffer::addImage(Rect cellArea, std::string encodedSixel)
{
    auto const hash = std::hash<std::string> {}(encodedSixel);
    _images.push_back(ImageRegion {
        .cellArea = cellArea,
        .encodedSixel = std::move(encodedSixel),
        .contentHash = hash,
    });
}

std::span<ImageRegion const> Buffer::images() const noexcept
{
    return _images;
}

void Buffer::clearImages() noexcept
{
    _images.clear();
}

void Buffer::writeTo(TerminalOutput& out) const
{
    for (auto const row: std::views::iota(0, _rows))
    {
        out.carriageReturn();
        for (auto col = 0; col < _cols;)
        {
            auto const& cell = at(row, col);
            if (cell.isContinuation())
            {
                ++col;
                continue;
            }
            out.writeText(cell.grapheme, cell.style);
            col += std::max(1, static_cast<int>(cell.width));
        }
        out.clearToEndOfLine();
        if (row < _rows - 1)
            out.linefeed();
    }

    // Position terminal cursor at the buffer's logical cursor location.
    if (_cursorVisible)
    {
        // After the loop, terminal cursor is at row (_rows - 1), end of content.
        auto const rowsUp = _rows - 1 - _cursor.y;
        if (rowsUp > 0)
            out.moveUp(rowsUp);
        out.carriageReturn();
        if (_cursor.x > 0)
            out.moveRight(_cursor.x);
        out.showCursor();
    }
}

} // namespace tui
