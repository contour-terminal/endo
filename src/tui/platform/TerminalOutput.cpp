// SPDX-License-Identifier: Apache-2.0
#include <tui/SgrBuilder.hpp>
#include <tui/TerminalOutput.hpp>
#include <tui/platform/PosixIO.hpp>

#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <format>

#include <sys/ioctl.h>

#include <poll.h>
#include <unistd.h>

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

    /// @brief Queries the terminal for XTVERSION and returns the response.
    ///
    /// Sends CSI > q and reads the DCS response with a short timeout.
    /// Response format: DCS > | <terminal-name-and-version> ST
    /// Example: "\033P>|kitty(0.26.5)\033\\"
    ///
    /// @pre The terminal must already be in raw mode (ECHO off) before calling.
    ///
    /// @param timeoutMs Timeout in milliseconds to wait for response.
    /// @return Terminal identification string, or empty if not supported/timeout.
    auto queryXTVersion(int timeoutMs = 100) -> std::string
    {
        // Send XTVERSION query: CSI > q
        static constexpr auto Query = "\033[>q";
        safeWrite(STDOUT_FILENO, Query, std::strlen(Query));

        std::string response;
        std::array<char, 256> buffer {};

        // Poll for response with timeout
        struct pollfd pfd {};
        pfd.fd = STDIN_FILENO;
        pfd.events = POLLIN;

        while (true)
        {
            int const ret = ::poll(&pfd, 1, timeoutMs);
            if (ret <= 0)
                break; // Timeout or error

            auto const n = safeRead(STDIN_FILENO, buffer.data(), buffer.size());
            if (n <= 0)
                break;

            response.append(buffer.data(), static_cast<size_t>(n));

            // Check for ST (String Terminator): ESC \ or 0x9C
            if (response.find("\033\\") != std::string::npos || response.find('\x9C') != std::string::npos)
                break;

            // Short timeout for subsequent reads
            timeoutMs = 10;
        }

        return response;
    }

    /// @brief Parses XTVERSION response to extract terminal name.
    ///
    /// Response format: DCS > | <name> ST
    /// Example: "\033P>|kitty(0.26.5)\033\\" -> "kitty"
    ///
    /// @param response The raw XTVERSION response.
    /// @return Lowercase terminal name, or empty if parsing failed.
    auto parseXTVersionName(std::string_view response) -> std::string
    {
        // Look for DCS > | prefix: ESC P > | or 0x90 > |
        auto pos = response.find("\033P>|");
        if (pos == std::string_view::npos)
        {
            pos = response.find("\x90>|");
            if (pos == std::string_view::npos)
                return {};
            pos += 3; // Skip 0x90 > |
        }
        else
        {
            pos += 4; // Skip ESC P > |
        }

        // Find ST: ESC \ or 0x9C
        auto end = response.find("\033\\", pos);
        if (end == std::string_view::npos)
        {
            end = response.find('\x9C', pos);
            if (end == std::string_view::npos)
                end = response.size();
        }

        std::string name(response.substr(pos, end - pos));

        // Extract just the terminal name (before version info)
        // e.g., "kitty(0.26.5)" -> "kitty", "contour 0.4.3" -> "contour"
        auto const parenPos = name.find('(');
        if (parenPos != std::string::npos)
            name = name.substr(0, parenPos);

        auto const spacePos = name.find(' ');
        if (spacePos != std::string::npos)
            name = name.substr(0, spacePos);

        // Convert to lowercase for comparison
        for (char& c: name)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        return name;
    }

    /// @brief Detects if the terminal supports the Kitty unscroll extension.
    ///
    /// Uses XTVERSION query (CSI > q) to identify the terminal.
    /// Known terminals that support CSI Ps + T (unscroll):
    /// - kitty
    /// - contour
    /// - mintty
    ///
    /// @return true if unscroll is likely supported.
    auto detectUnscrollSupport() -> bool
    {
        auto const response = queryXTVersion();
        if (response.empty())
            return false;

        auto const name = parseXTVersionName(response);
        if (name.empty())
            return false;

        // Terminals known to support CSI Ps + T (unscroll)
        return name == "kitty" || name == "contour" || name == "mintty";
    }

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

SyncGuard::SyncGuard(): _handle(-1)
{
}

SyncGuard::SyncGuard(NativeHandle handle): _handle(handle)
{
    if (_handle >= 0)
    {
        static constexpr auto Begin = "\033[?2026h";
        safeWrite(_handle, Begin, std::strlen(Begin));
    }
}

SyncGuard::~SyncGuard()
{
    if (_handle >= 0)
    {
        static constexpr auto End = "\033[?2026l";
        safeWrite(_handle, End, std::strlen(End));
    }
}

SyncGuard::SyncGuard(SyncGuard&& other) noexcept: _handle(other._handle)
{
    other._handle = -1;
}

auto SyncGuard::operator=(SyncGuard&& other) noexcept -> SyncGuard&
{
    if (this != &other)
    {
        if (_handle >= 0)
        {
            static constexpr auto End = "\033[?2026l";
            safeWrite(_handle, End, std::strlen(End));
        }
        _handle = other._handle;
        other._handle = -1;
    }
    return *this;
}

// --- TerminalOutput ---

auto TerminalOutput::initialize() -> VoidResult
{
    updateDimensions();
    return {};
}

void TerminalOutput::detectCapabilities()
{
    _unscrollSupported = detectUnscrollSupport();
}

void TerminalOutput::writeText(std::string_view text, Style const& style)
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

void TerminalOutput::carriageReturn()
{
    _buffer += '\r';
}

void TerminalOutput::linefeed()
{
    _buffer += '\n';
}

void TerminalOutput::clearToEndOfDisplay()
{
    _buffer += "\033[J";
}

void TerminalOutput::requestCellSize()
{
    _buffer += "\033[16t";
}

void TerminalOutput::requestCursorPosition()
{
    _buffer += "\033[6n";
}

void TerminalOutput::requestDecMode(int mode)
{
    _buffer += std::format("\033[?{}$p", mode);
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

void TerminalOutput::disableReflow()
{
    _buffer += "\033[?2028l";
}

void TerminalOutput::enableReflow()
{
    _buffer += "\033[?2028h";
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

void TerminalOutput::setCursorShape(CursorShape shape)
{
    _buffer += std::format("\033[{} q", static_cast<int>(shape));
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
    _buffer += "\033P0;1q";
    _buffer.append(sixelData);
    _buffer += "\033\\";
}

void TerminalOutput::copyToClipboard(std::string_view text)
{
    // OSC 52 format: 'ESC ] 52 ; c ; <base64-data> ESC \'
    // 'c' means system clipboard (could also use 'p' for primary selection)
    _buffer += "\033]52;c;";
    _buffer += base64Encode(text);
    _buffer += "\033\\";
}

void TerminalOutput::unscroll(int n)
{
    // Kitty unscroll extension: CSI Ps + T
    // This is an extension to SD (Scroll Down / Pan Up) that restores
    // lines from the scrollback buffer instead of inserting blank lines.
    // See: https://sw.kovidgoyal.net/kitty/unscroll/
    if (n > 0)
        _buffer += std::format("\033[{}+T", n);
}

bool TerminalOutput::supportsUnscroll() const noexcept
{
    return _unscrollSupported;
}

void TerminalOutput::flush()
{
    if (!_buffer.empty())
    {
        safeWrite(STDOUT_FILENO, _buffer.data(), _buffer.size());
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
    _buffer += buildSgrSequence(style);
}

void TerminalOutput::appendSgrReset()
{
    _buffer += "\033[m";
}

} // namespace tui
