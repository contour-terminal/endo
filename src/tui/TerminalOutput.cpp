// SPDX-License-Identifier: Apache-2.0
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <format>

#include <sys/ioctl.h>

#include <poll.h>
#include <termios.h>
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

    /// @brief Queries the terminal for XTVERSION and returns the response.
    ///
    /// Sends CSI > q and reads the DCS response with a short timeout.
    /// Response format: DCS > | <terminal-name-and-version> ST
    /// Example: "\033P>|kitty(0.26.5)\033\\"
    ///
    /// @param timeoutMs Timeout in milliseconds to wait for response.
    /// @return Terminal identification string, or empty if not supported/timeout.
    auto queryXTVersion(int timeoutMs = 100) -> std::string
    {
        // Save current terminal attributes
        struct termios origTermios {};
        struct termios rawTermios {};
        bool needRestore = false;

        if (tcgetattr(STDIN_FILENO, &origTermios) == 0)
        {
            rawTermios = origTermios;
            // Set raw mode for reliable reading
            rawTermios.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO);
            rawTermios.c_cc[VMIN] = 0;
            rawTermios.c_cc[VTIME] = 0;
            if (tcsetattr(STDIN_FILENO, TCSANOW, &rawTermios) == 0)
                needRestore = true;
        }

        // Send XTVERSION query: CSI > q
        static constexpr auto Query = "\033[>q";
        static_cast<void>(::write(STDOUT_FILENO, Query, std::strlen(Query)));

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

            auto const n = ::read(STDIN_FILENO, buffer.data(), buffer.size());
            if (n <= 0)
                break;

            response.append(buffer.data(), static_cast<size_t>(n));

            // Check for ST (String Terminator): ESC \ or 0x9C
            if (response.find("\033\\") != std::string::npos || response.find('\x9C') != std::string::npos)
                break;

            // Short timeout for subsequent reads
            timeoutMs = 10;
        }

        // Restore terminal attributes
        if (needRestore)
            tcsetattr(STDIN_FILENO, TCSANOW, &origTermios);

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
    _unscrollSupported = detectUnscrollSupport();
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
    // Compute effective underline style
    auto const effectiveUnderline = style.underlineStyle != UnderlineStyle::None
                                        ? style.underlineStyle
                                        : (style.underline ? UnderlineStyle::Single : UnderlineStyle::None);
    auto const isDefaultUlColor = std::holds_alternative<std::monostate>(style.underlineColor);

    // Check if style is default (no attributes set)
    auto const isDefaultFg = std::holds_alternative<std::monostate>(style.fg);
    auto const isDefaultBg = std::holds_alternative<std::monostate>(style.bg);
    if (isDefaultFg && isDefaultBg && !style.bold && !style.italic && !style.strikethrough && !style.dim
        && !style.inverse && effectiveUnderline == UnderlineStyle::None)
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
    if (effectiveUnderline != UnderlineStyle::None)
    {
        appendSep();
        _buffer += std::format("4:{}", static_cast<int>(effectiveUnderline));
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

    // Underline color (SGR 58) — emitted as a separate sequence to avoid exceeding
    // the 16-element parameter array limit in some terminals (e.g. contour) when
    // combined with fg/bg colors in the same sequence.
    if (!isDefaultUlColor)
    {
        if (auto const* idx = std::get_if<std::uint8_t>(&style.underlineColor))
            _buffer += std::format("\033[58:5:{}m", *idx);
        else if (auto const* rgb = std::get_if<RgbColor>(&style.underlineColor))
            _buffer += std::format("\033[58:2:{}:{}:{}m", rgb->r, rgb->g, rgb->b);
    }
}

void TerminalOutput::appendSgrReset()
{
    _buffer += "\033[m";
}

} // namespace tui
