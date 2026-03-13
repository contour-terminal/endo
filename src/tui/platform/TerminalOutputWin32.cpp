// SPDX-License-Identifier: Apache-2.0
#include <tui/SgrBuilder.hpp>
#include <tui/TerminalOutput.hpp>

#if defined(_WIN32)

    #include <tui/platform/Win32Utf.hpp>

    #include <array>
    #include <cctype>
    #include <cstdlib>
    #include <cstring>
    #include <format>
    #include <string>
    #include <vector>

    #include <windows.h>

namespace tui
{

namespace
{
    /// @brief Queries the terminal for XTVERSION and returns the response.
    ///
    /// Sends CSI > q and reads the DCS response with a short timeout.
    /// Response format: DCS > | <terminal-name-and-version> ST
    ///
    /// @param timeoutMs Timeout in milliseconds to wait for response.
    /// @return Terminal identification string, or empty if not supported/timeout.
    auto queryXTVersion(int timeoutMs = 100) -> std::string
    {
        auto const hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
        auto const hStdin = GetStdHandle(STD_INPUT_HANDLE);

        if (hStdout == INVALID_HANDLE_VALUE || hStdin == INVALID_HANDLE_VALUE)
            return {};

        // Save current console mode and set raw mode for reliable reading
        DWORD origMode = 0;
        GetConsoleMode(hStdin, &origMode);

        DWORD const rawMode = ENABLE_VIRTUAL_TERMINAL_INPUT;
        SetConsoleMode(hStdin, rawMode);

        // Send XTVERSION query: CSI > q
        static constexpr auto Query = "\033[>q";
        DWORD written = 0;
        WriteFile(hStdout, Query, static_cast<DWORD>(std::strlen(Query)), &written, nullptr);

        std::string response;

        // Wait for response with timeout, processing input records
        auto const deadline = GetTickCount64() + static_cast<ULONGLONG>(timeoutMs);

        while (true)
        {
            auto const now = GetTickCount64();
            if (now >= deadline)
                break;

            auto const remaining = static_cast<DWORD>(deadline - now);
            auto const waitResult = WaitForSingleObject(hStdin, remaining);

            if (waitResult != WAIT_OBJECT_0)
                break;

            DWORD numEvents = 0;
            if (!GetNumberOfConsoleInputEvents(hStdin, &numEvents) || numEvents == 0)
                break;

            auto records = std::vector<INPUT_RECORD>(numEvents);
            DWORD eventsRead = 0;
            if (!ReadConsoleInput(hStdin, records.data(), numEvents, &eventsRead))
                break;

            for (DWORD i = 0; i < eventsRead; ++i)
            {
                if (records[i].EventType == KEY_EVENT && records[i].Event.KeyEvent.bKeyDown)
                {
                    auto const wc = records[i].Event.KeyEvent.uChar.UnicodeChar;
                    if (wc != 0)
                        appendUtf16AsUtf8(response, wc);
                }
            }

            // Check for ST (String Terminator): ESC \ or 0x9C
            if (response.find("\033\\") != std::string::npos || response.find('\x9C') != std::string::npos)
                break;
        }

        // Restore console mode
        SetConsoleMode(hStdin, origMode);

        return response;
    }

    /// @brief Parses XTVERSION response to extract terminal name.
    auto parseXTVersionName(std::string_view response) -> std::string
    {
        auto pos = response.find("\033P>|");
        if (pos == std::string_view::npos)
        {
            pos = response.find("\x90>|");
            if (pos == std::string_view::npos)
                return {};
            pos += 3;
        }
        else
        {
            pos += 4;
        }

        auto end = response.find("\033\\", pos);
        if (end == std::string_view::npos)
        {
            end = response.find('\x9C', pos);
            if (end == std::string_view::npos)
                end = response.size();
        }

        std::string name(response.substr(pos, end - pos));

        auto const parenPos = name.find('(');
        if (parenPos != std::string::npos)
            name = name.substr(0, parenPos);

        auto const spacePos = name.find(' ');
        if (spacePos != std::string::npos)
            name = name.substr(0, spacePos);

        for (char& c: name)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        return name;
    }

    /// @brief Detects if the terminal supports the Kitty unscroll extension.
    auto detectUnscrollSupport() -> bool
    {
        auto const response = queryXTVersion();
        if (response.empty())
            return false;

        auto const name = parseXTVersionName(response);
        if (name.empty())
            return false;

        return name == "kitty" || name == "contour" || name == "mintty";
    }

    /// @brief Encodes data to base64.
    auto base64Encode(std::string_view data) -> std::string
    {
        constexpr std::array<char, 64> Base64Chars = { 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K',
                                                       'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V',
                                                       'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f', 'g',
                                                       'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r',
                                                       's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '0', '1', '2',
                                                       '3', '4', '5', '6', '7', '8', '9', '+', '/' };

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

SyncGuard::SyncGuard(): _handle(reinterpret_cast<NativeHandle>(-1))
{
}

SyncGuard::SyncGuard(NativeHandle handle): _handle(handle)
{
    if (_handle != reinterpret_cast<NativeHandle>(-1))
    {
        static constexpr auto Begin = "\033[?2026h";
        DWORD written = 0;
        WriteFile(_handle, Begin, static_cast<DWORD>(std::strlen(Begin)), &written, nullptr);
    }
}

SyncGuard::~SyncGuard()
{
    if (_handle != reinterpret_cast<NativeHandle>(-1))
    {
        static constexpr auto End = "\033[?2026l";
        DWORD written = 0;
        WriteFile(_handle, End, static_cast<DWORD>(std::strlen(End)), &written, nullptr);
    }
}

SyncGuard::SyncGuard(SyncGuard&& other) noexcept: _handle(other._handle)
{
    other._handle = reinterpret_cast<NativeHandle>(-1);
}

auto SyncGuard::operator=(SyncGuard&& other) noexcept -> SyncGuard&
{
    if (this != &other)
    {
        if (_handle != reinterpret_cast<NativeHandle>(-1))
        {
            static constexpr auto End = "\033[?2026l";
            DWORD written = 0;
            WriteFile(_handle, End, static_cast<DWORD>(std::strlen(End)), &written, nullptr);
        }
        _handle = other._handle;
        other._handle = reinterpret_cast<NativeHandle>(-1);
    }
    return *this;
}

// --- TerminalOutput ---

auto TerminalOutput::initialize() -> VoidResult
{
    updateDimensions();
    _unscrollSupported = detectUnscrollSupport();
    return {};
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
    flush();
    return SyncGuard(GetStdHandle(STD_OUTPUT_HANDLE));
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
    _buffer += "\x1b"
               "7";
}

void TerminalOutput::restoreCursor()
{
    _buffer += "\x1b"
               "8";
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
    _buffer += "\033]52;c;";
    _buffer += base64Encode(text);
    _buffer += "\033\\";
}

void TerminalOutput::unscroll(int n)
{
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
        auto const hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD written = 0;
        WriteFile(hStdout, _buffer.data(), static_cast<DWORD>(_buffer.size()), &written, nullptr);
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
    CONSOLE_SCREEN_BUFFER_INFO csbi {};
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
    {
        _cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        _rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    }
}

void TerminalOutput::detectCapabilities()
{
    _unscrollSupported = detectUnscrollSupport();
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

#endif // _WIN32
