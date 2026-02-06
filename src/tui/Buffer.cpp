// SPDX-License-Identifier: Apache-2.0
#include "Buffer.hpp"

#include <stdexcept>

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wold-style-cast"
#endif
#include <libunicode/utf8_grapheme_segmenter.h>
#include <libunicode/width.h>
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
        _cursor = { 0, 0 };
        return;
    }

    std::vector<Cell> newCells(static_cast<size_t>(rows) * static_cast<size_t>(cols));

    // Initialize all cells to spaces
    for (auto& cell: newCells)
        cell.reset();

    // Copy existing content
    int copyRows = std::min(_rows, rows);
    int copyCols = std::min(_cols, cols);

    for (int r = 0; r < copyRows; ++r)
    {
        for (int c = 0; c < copyCols; ++c)
        {
            size_t oldIdx = static_cast<size_t>(r) * static_cast<size_t>(_cols) + static_cast<size_t>(c);
            size_t newIdx = static_cast<size_t>(r) * static_cast<size_t>(cols) + static_cast<size_t>(c);
            newCells[newIdx] = _cells[oldIdx];
        }
    }

    _cells = std::move(newCells);
    _rows = rows;
    _cols = cols;

    // Clamp cursor to new bounds
    _cursor.x = std::min(_cursor.x, _cols - 1);
    _cursor.y = std::min(_cursor.y, _rows - 1);
    if (_cursor.x < 0)
        _cursor.x = 0;
    if (_cursor.y < 0)
        _cursor.y = 0;
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
}

void Buffer::clearRect(Rect area, Style const& style)
{
    // Clamp area to buffer bounds
    int startRow = std::max(0, area.y);
    int startCol = std::max(0, area.x);
    int endRow = std::min(_rows, area.bottom());
    int endCol = std::min(_cols, area.right());

    for (int row = startRow; row < endRow; ++row)
    {
        for (int col = startCol; col < endCol; ++col)
        {
            _cells[index(row, col)].reset(style);
        }
    }
}

int Buffer::putString(int row, int col, std::string_view text, Style const& style)
{
    if (row < 0 || row >= _rows || col < 0)
        return 0;

    int currentCol = col;

    // Use libunicode to segment into grapheme clusters
    auto segmenter = unicode::utf8_grapheme_segmenter(text);

    for (auto it = segmenter.begin(); it != segmenter.end(); ++it)
    {
        if (currentCol >= _cols)
            break;

        auto const& cluster = *it;

        // Calculate display width by iterating over codepoints in the cluster
        int clusterWidth = 0;
        for (char32_t cp: cluster)
            clusterWidth += unicode::width(cp);

        if (clusterWidth == 0)
            clusterWidth = 1; // Ensure at least 1 column

        // Check if it fits
        if (currentCol + clusterWidth > _cols)
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

    for (int row = startRow; row < endRow; ++row)
    {
        for (int col = startCol; col < endCol; ++col)
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

} // namespace tui
