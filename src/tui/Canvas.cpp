// SPDX-License-Identifier: Apache-2.0
#include "Canvas.hpp"

#include <tui/Theme.hpp>

namespace tui
{

Canvas::Canvas(Buffer& buffer, Rect area, Theme const& theme): _buffer(buffer), _area(area), _theme(theme)
{
}

void Canvas::put(int row, int col, std::string_view grapheme, Style const& style)
{
    if (!inBounds(row, col))
        return;

    int bufRow = toBufferRow(row);
    int bufCol = toBufferCol(col);

    if (Cell* cell = _buffer.tryAt(bufRow, bufCol))
    {
        cell->grapheme = std::string(grapheme);
        cell->style = style;
        cell->width = 1;
    }
}

int Canvas::putString(int row, int col, std::string_view text, Style const& style)
{
    if (row < 0 || row >= _area.height || col < 0)
        return 0;

    // Calculate available width from col to right edge
    int availableWidth = _area.width - col;
    if (availableWidth <= 0)
        return 0;

    int bufRow = toBufferRow(row);
    int bufCol = toBufferCol(col);

    // Use buffer's putString but limit to available width
    // We need to handle clipping ourselves since buffer doesn't know our bounds
    int written = _buffer.putString(bufRow, bufCol, text, style);

    // Clamp to available width
    return std::min(written, availableWidth);
}

void Canvas::fill(Rect area, char ch, Style const& style)
{
    Rect clipped = clipToLocal(area);
    if (clipped.empty())
        return;

    Rect bufRect = toBufferRect(clipped);
    _buffer.fill(bufRect, ch, style);
}

void Canvas::clear(Style const& style)
{
    _buffer.clearRect(_area, style);
}

void Canvas::drawBox(Rect area, BorderStyle border, Style const& style)
{
    drawBox(area, border, style, "", TitleAlign::Left);
}

void Canvas::drawBox(
    Rect area, BorderStyle border, Style const& style, std::string_view title, TitleAlign align)
{
    Rect clipped = clipToLocal(area);
    if (clipped.width < 2 || clipped.height < 2)
        return;

    auto const chars = BorderChars::fromStyle(border);

    int top = clipped.y;
    int bottom = clipped.bottom() - 1;
    int left = clipped.x;
    int right = clipped.right() - 1;

    // Draw corners
    put(top, left, chars.topLeft, style);
    put(top, right, chars.topRight, style);
    put(bottom, left, chars.bottomLeft, style);
    put(bottom, right, chars.bottomRight, style);

    // Draw horizontal lines
    for (int col = left + 1; col < right; ++col)
    {
        put(top, col, chars.horizontal, style);
        put(bottom, col, chars.horizontal, style);
    }

    // Draw vertical lines
    for (int row = top + 1; row < bottom; ++row)
    {
        put(row, left, chars.vertical, style);
        put(row, right, chars.vertical, style);
    }

    // Draw title if provided
    if (!title.empty() && clipped.width > 4)
    {
        int maxTitleWidth = clipped.width - 4; // Leave space for corners and padding
        std::string_view displayTitle = title.substr(0, static_cast<size_t>(maxTitleWidth));

        int titleCol = left + 2;
        switch (align)
        {
            case TitleAlign::Left: titleCol = left + 2; break;
            case TitleAlign::Center:
                titleCol = left + (clipped.width - static_cast<int>(displayTitle.size())) / 2;
                break;
            case TitleAlign::Right: titleCol = right - static_cast<int>(displayTitle.size()) - 1; break;
        }

        putString(top, titleCol, displayTitle, style);
    }
}

void Canvas::drawHLine(int row, int startCol, int width, std::string_view ch, Style const& style)
{
    if (row < 0 || row >= _area.height)
        return;

    int endCol = std::min(startCol + width, _area.width);
    for (int col = std::max(0, startCol); col < endCol; ++col)
    {
        put(row, col, ch, style);
    }
}

void Canvas::drawVLine(int col, int startRow, int height, std::string_view ch, Style const& style)
{
    if (col < 0 || col >= _area.width)
        return;

    int endRow = std::min(startRow + height, _area.height);
    for (int row = std::max(0, startRow); row < endRow; ++row)
    {
        put(row, col, ch, style);
    }
}

void Canvas::setCursor(int row, int col)
{
    if (inBounds(row, col))
    {
        _buffer.setCursor(toBufferRow(row), toBufferCol(col));
        _buffer.setCursorVisible(true);
    }
}

void Canvas::hideCursor()
{
    _buffer.setCursorVisible(false);
}

Canvas Canvas::subcanvas(Rect area) const
{
    // Clip the requested area to our bounds
    Rect clipped = clipToLocal(area);

    // Translate to buffer coordinates
    Rect bufferArea = toBufferRect(clipped);

    return Canvas(_buffer, bufferArea, _theme);
}

Rect Canvas::clipToLocal(Rect localArea) const noexcept
{
    // Intersect with our local bounds (0,0 to width,height)
    return localArea.intersect(Rect { 0, 0, _area.width, _area.height });
}

Rect Canvas::toBufferRect(Rect localArea) const noexcept
{
    return localArea.offset(_area.x, _area.y);
}

} // namespace tui
