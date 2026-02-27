// SPDX-License-Identifier: Apache-2.0
#include <tui/MockTerminalOutput.hpp>

#include <algorithm>

namespace tui
{

MockTerminalOutput::MockTerminalOutput(int cols, int rows): _cols(cols), _rows(rows)
{
}

auto MockTerminalOutput::initialize() -> VoidResult
{
    return {};
}

void MockTerminalOutput::writeText(std::string_view text, [[maybe_unused]] Style const& style)
{
    // Advance cursor column by display width of text.
    // No auto-wrap: real terminals use "deferred wrap" at column boundary,
    // and inline rendering uses \r at each row start to reset the column.
    // Row advancement is handled by \n in writeRaw().
    for (auto const ch: text)
    {
        if ((static_cast<unsigned char>(ch) & 0xC0) == 0x80)
            continue; // Skip UTF-8 continuation bytes
        ++_cursorCol;
    }
}

void MockTerminalOutput::writeRaw(std::string_view text)
{
    for (auto const ch: text)
    {
        if (ch == '\n')
        {
            ++_cursorRow;
            if (_cursorRow >= _rows)
            {
                _cursorRow = _rows - 1;
                ++_scrollCount;
            }
        }
        else if (ch == '\r')
        {
            _cursorCol = 0;
        }
        else if (ch == '\033')
        {
            // Stop processing — the rest is a VT escape sequence.
            // The mock doesn't parse escape sequences; callers should use
            // the semantic methods (moveUp, clearLine, etc.) instead.
            return;
        }
        else
        {
            if ((static_cast<unsigned char>(ch) & 0xC0) == 0x80)
                continue; // Skip UTF-8 continuation bytes
            // No auto-wrap for printable characters (same as writeText).
            ++_cursorCol;
        }
    }
}

void MockTerminalOutput::moveTo(int row, int col)
{
    // moveTo uses 1-based coordinates; convert to 0-based.
    _cursorRow = std::clamp(row - 1, 0, _rows - 1);
    _cursorCol = std::clamp(col - 1, 0, _cols - 1);
}

void MockTerminalOutput::moveUp(int n)
{
    _cursorRow = std::max(0, _cursorRow - n);
}

void MockTerminalOutput::moveDown(int n)
{
    _cursorRow = std::min(_rows - 1, _cursorRow + n);
}

void MockTerminalOutput::moveLeft(int n)
{
    _cursorCol = std::max(0, _cursorCol - n);
}

void MockTerminalOutput::moveRight(int n)
{
    _cursorCol = std::min(_cols - 1, _cursorCol + n);
}

void MockTerminalOutput::carriageReturn()
{
    _cursorCol = 0;
}

void MockTerminalOutput::linefeed()
{
    ++_cursorRow;
    if (_cursorRow >= _rows)
    {
        _cursorRow = _rows - 1;
        ++_scrollCount;
    }
}

void MockTerminalOutput::clearToEndOfDisplay()
{
    // No-op on mock canvas — no pixel/character buffer to clear.
}

void MockTerminalOutput::requestCellSize()
{
    // No-op — mock does not respond to terminal queries.
}

void MockTerminalOutput::requestCursorPosition()
{
    // No-op — mock does not respond to terminal queries.
}

void MockTerminalOutput::requestDecMode(int /*mode*/)
{
    // No-op — mock does not respond to terminal queries.
}

void MockTerminalOutput::clearLine()
{
    // No-op on mock canvas — just semantic tracking.
}

void MockTerminalOutput::clearToEndOfLine()
{
    // No-op on mock canvas.
}

void MockTerminalOutput::clearToStartOfLine()
{
    // No-op on mock canvas.
}

void MockTerminalOutput::clearScreen()
{
    _cursorRow = 0;
    _cursorCol = 0;
}

void MockTerminalOutput::clearScrollback()
{
    // No-op.
}

void MockTerminalOutput::enterAltScreen()
{
    // No-op.
}

void MockTerminalOutput::leaveAltScreen()
{
    // No-op.
}

auto MockTerminalOutput::syncGuard() -> SyncGuard
{
    return SyncGuard(); // No-op guard (handle == -1).
}

void MockTerminalOutput::setDoubleWidth()
{
    // No-op.
}

void MockTerminalOutput::setDoubleHeightTop()
{
    // No-op.
}

void MockTerminalOutput::setDoubleHeightBottom()
{
    // No-op.
}

void MockTerminalOutput::setSingleWidth()
{
    // No-op.
}

void MockTerminalOutput::disableReflow()
{
    // No-op.
}

void MockTerminalOutput::enableReflow()
{
    // No-op.
}

void MockTerminalOutput::showCursor()
{
    _cursorVisible = true;
}

void MockTerminalOutput::hideCursor()
{
    _cursorVisible = false;
}

void MockTerminalOutput::saveCursor()
{
    _savedCursorRow = _cursorRow;
    _savedCursorCol = _cursorCol;
}

void MockTerminalOutput::restoreCursor()
{
    _cursorRow = _savedCursorRow;
    _cursorCol = _savedCursorCol;
}

void MockTerminalOutput::setCursorShape([[maybe_unused]] CursorShape shape)
{
    // No-op.
}

void MockTerminalOutput::setScrollRegion([[maybe_unused]] int top, [[maybe_unused]] int bottom)
{
    // No-op.
}

void MockTerminalOutput::resetScrollRegion()
{
    // No-op.
}

void MockTerminalOutput::writeSixel([[maybe_unused]] std::string_view sixelData)
{
    // No-op.
}

void MockTerminalOutput::copyToClipboard([[maybe_unused]] std::string_view text)
{
    // No-op.
}

void MockTerminalOutput::unscroll([[maybe_unused]] int n)
{
    // No-op.
}

bool MockTerminalOutput::supportsUnscroll() const noexcept
{
    return false;
}

void MockTerminalOutput::flush()
{
    ++_flushCount;
}

auto MockTerminalOutput::columns() const noexcept -> int
{
    return _cols;
}

auto MockTerminalOutput::rows() const noexcept -> int
{
    return _rows;
}

void MockTerminalOutput::updateDimensions()
{
    // No-op — dimensions are fixed for the mock.
}

int MockTerminalOutput::cursorRow() const noexcept
{
    return _cursorRow;
}

int MockTerminalOutput::cursorCol() const noexcept
{
    return _cursorCol;
}

bool MockTerminalOutput::cursorVisible() const noexcept
{
    return _cursorVisible;
}

int MockTerminalOutput::scrollCount() const noexcept
{
    return _scrollCount;
}

int MockTerminalOutput::flushCount() const noexcept
{
    return _flushCount;
}

} // namespace tui
