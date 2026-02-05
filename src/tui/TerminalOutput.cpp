// SPDX-License-Identifier: Apache-2.0
#include <array>
#include <cstring>
#include <format>

#include <sys/ioctl.h>

#include <unistd.h>

#include <tui/TerminalOutput.hpp>

namespace tui
{

namespace
{
    /// @brief Base64 encoding table.
    constexpr std::array<char, 64> Base64Chars = { 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K',
                                                   'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V',
                                                   'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f', 'g',
                                                   'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r',
                                                   's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '0', '1', '2',
                                                   '3', '4', '5', '6', '7', '8', '9', '+', '/' };

    /// @brief Encodes data to base64.
    /// @param data The data to encode.
    /// @return Base64-encoded string.
    auto base64Encode(std::string_view data) -> std::string
    {
        auto result = std::string {};
        result.reserve(((data.size() + 2) / 3) * 4);

        auto const* bytes = reinterpret_cast<unsigned char const*>(data.data());
        auto const len = data.size();

        for (std::size_t i = 0; i < len; i += 3)
        {
            auto const b0 = bytes[i];
            auto const b1 = (i + 1 < len) ? bytes[i + 1] : 0;
            auto const b2 = (i + 2 < len) ? bytes[i + 2] : 0;

            result += Base64Chars[(b0 >> 2) & 0x3F];
            result += Base64Chars[((b0 << 4) | (b1 >> 4)) & 0x3F];

            if (i + 1 < len)
                result += Base64Chars[((b1 << 2) | (b2 >> 6)) & 0x3F];
            else
                result += '=';

            if (i + 2 < len)
                result += Base64Chars[b2 & 0x3F];
            else
                result += '=';
        }

        return result;
    }
} // namespace

// --- SyncGuard ---

SyncGuard::SyncGuard(int fd): _fd(fd)
{
    static constexpr auto Begin = "\033[?2026h";
    static_cast<void>(::write(_fd, Begin, std::strlen(Begin)));
}

SyncGuard::~SyncGuard()
{
    static constexpr auto End = "\033[?2026l";
    static_cast<void>(::write(_fd, End, std::strlen(End)));
}

// --- TerminalOutput ---

auto TerminalOutput::initialize() -> VoidResult
{
    updateDimensions();
    return {};
}

void TerminalOutput::write(std::string_view text, Style const& style)
{
    appendSgr(style);
    _buffer.append(text);
    appendSgrReset();
}

void TerminalOutput::writeRaw(std::string_view text)
{
    _buffer.append(text);
}

void TerminalOutput::moveTo(int row, int col)
{
    _buffer += std::format("\033[{};{}H", row, col);
}

void TerminalOutput::moveUp(int n)
{
    if (n > 0)
        _buffer += std::format("\033[{}A", n);
}

void TerminalOutput::moveDown(int n)
{
    if (n > 0)
        _buffer += std::format("\033[{}B", n);
}

void TerminalOutput::moveLeft(int n)
{
    if (n > 0)
        _buffer += std::format("\033[{}D", n);
}

void TerminalOutput::moveRight(int n)
{
    if (n > 0)
        _buffer += std::format("\033[{}C", n);
}

void TerminalOutput::clearLine()
{
    _buffer += "\033[2K";
}

void TerminalOutput::clearToEndOfLine()
{
    _buffer += "\033[K";
}

void TerminalOutput::clearToStartOfLine()
{
    _buffer += "\033[1K";
}

void TerminalOutput::clearScreen()
{
    _buffer += "\033[2J\033[H";
}

void TerminalOutput::clearScrollback()
{
    _buffer += "\033[3J";
}

void TerminalOutput::enterAltScreen()
{
    _buffer += "\033[?1049h";
}

void TerminalOutput::leaveAltScreen()
{
    _buffer += "\033[?1049l";
}

auto TerminalOutput::syncGuard() -> SyncGuard
{
    flush(); // Flush any pending output before entering sync mode
    return SyncGuard(STDOUT_FILENO);
}

void TerminalOutput::setDoubleWidth()
{
    _buffer += "\033#6";
}

void TerminalOutput::setDoubleHeightTop()
{
    _buffer += "\033#3";
}

void TerminalOutput::setDoubleHeightBottom()
{
    _buffer += "\033#4";
}

void TerminalOutput::setSingleWidth()
{
    _buffer += "\033#5";
}

void TerminalOutput::showCursor()
{
    _buffer += "\033[?25h";
}

void TerminalOutput::hideCursor()
{
    _buffer += "\033[?25l";
}

void TerminalOutput::saveCursor()
{
    _buffer += "\0337";
}

void TerminalOutput::restoreCursor()
{
    _buffer += "\0338";
}

void TerminalOutput::setScrollRegion(int top, int bottom)
{
    _buffer += std::format("\033[{};{}r", top, bottom);
}

void TerminalOutput::resetScrollRegion()
{
    _buffer += "\033[r";
}

void TerminalOutput::writeSixel(std::string_view sixelData)
{
    _buffer += "\033Pq";
    _buffer.append(sixelData);
    _buffer += "\033\\";
}

void TerminalOutput::copyToClipboard(std::string_view text)
{
    // OSC 52 format: ESC ] 52 ; c ; <base64-data> ESC \
    // 'c' means system clipboard (could also use 'p' for primary selection)
    _buffer += "\033]52;c;";
    _buffer += base64Encode(text);
    _buffer += "\033\\";
}

void TerminalOutput::flush()
{
    if (!_buffer.empty())
    {
        static_cast<void>(::write(STDOUT_FILENO, _buffer.data(), _buffer.size()));
        _buffer.clear();
    }
}

auto TerminalOutput::columns() const noexcept -> int
{
    return _cols;
}

auto TerminalOutput::rows() const noexcept -> int
{
    return _rows;
}

void TerminalOutput::updateDimensions()
{
    auto ws = winsize {};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0)
    {
        _cols = ws.ws_col;
        _rows = ws.ws_row;
    }
}

void TerminalOutput::appendSgr(Style const& style)
{
    // Check if style is default (no attributes set)
    auto const isDefaultFg = std::holds_alternative<std::monostate>(style.fg);
    auto const isDefaultBg = std::holds_alternative<std::monostate>(style.bg);
    if (isDefaultFg && isDefaultBg && !style.bold && !style.italic && !style.underline && !style.strikethrough
        && !style.dim && !style.inverse)
        return;

    _buffer += "\033[";
    auto needSemicolon = false;
    auto const appendSep = [&]() {
        if (needSemicolon)
            _buffer += ';';
        needSemicolon = true;
    };

    if (style.bold)
    {
        appendSep();
        _buffer += '1';
    }
    if (style.dim)
    {
        appendSep();
        _buffer += '2';
    }
    if (style.italic)
    {
        appendSep();
        _buffer += '3';
    }
    if (style.underline)
    {
        appendSep();
        _buffer += '4';
    }
    if (style.inverse)
    {
        appendSep();
        _buffer += '7';
    }
    if (style.strikethrough)
    {
        appendSep();
        _buffer += '9';
    }

    // Foreground color
    if (auto const* idx = std::get_if<std::uint8_t>(&style.fg))
    {
        appendSep();
        _buffer += std::format("38;5;{}", *idx);
    }
    else if (auto const* rgb = std::get_if<RgbColor>(&style.fg))
    {
        appendSep();
        _buffer += std::format("38;2;{};{};{}", rgb->r, rgb->g, rgb->b);
    }

    // Background color
    if (auto const* idx = std::get_if<std::uint8_t>(&style.bg))
    {
        appendSep();
        _buffer += std::format("48;5;{}", *idx);
    }
    else if (auto const* rgb = std::get_if<RgbColor>(&style.bg))
    {
        appendSep();
        _buffer += std::format("48;2;{};{};{}", rgb->r, rgb->g, rgb->b);
    }

    _buffer += 'm';
}

void TerminalOutput::appendSgrReset()
{
    _buffer += "\033[m";
}

} // namespace tui
