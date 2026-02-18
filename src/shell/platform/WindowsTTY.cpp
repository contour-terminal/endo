// SPDX-License-Identifier: Apache-2.0
#include <shell/Error.hpp>
#include <shell/TTY.hpp>

#include <chrono>
#include <cstring>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <platform/Types.hpp>

#if defined(_WIN32)
    #include <windows.h>

namespace endo
{

WindowsTTY::WindowsTTY()
{
    _hStdin = GetStdHandle(STD_INPUT_HANDLE);
    _hStdout = GetStdHandle(STD_OUTPUT_HANDLE);

    if (_hStdin == INVALID_HANDLE_VALUE || _hStdout == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Failed to get console handles");

    // Save original console modes
    DWORD inputMode = 0;
    if (!GetConsoleMode(_hStdin, &inputMode))
        throw std::runtime_error("Failed to get console input mode");
    _originalInputMode = inputMode;

    DWORD outputMode = 0;
    if (GetConsoleMode(_hStdout, &outputMode))
        _originalOutputMode = outputMode;
}

WindowsTTY::~WindowsTTY()
{
    restoreMode();
}

WindowsTTY& WindowsTTY::instance()
{
    static WindowsTTY inst;
    return inst;
}

NativeHandle WindowsTTY::inputFd() const noexcept
{
    return _hStdin;
}

NativeHandle WindowsTTY::outputFd() const noexcept
{
    return _hStdout;
}

bool WindowsTTY::isTerminal() const noexcept
{
    DWORD mode;
    return GetConsoleMode(_hStdin, &mode) != 0;
}

std::expected<TerminalSize, ShellError> WindowsTTY::getSize() const
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(_hStdout, &csbi))
        return std::unexpected(ShellError::IoError);

    return TerminalSize { .rows = static_cast<uint16_t>(csbi.srWindow.Bottom - csbi.srWindow.Top + 1),
                          .cols = static_cast<uint16_t>(csbi.srWindow.Right - csbi.srWindow.Left + 1) };
}

void WindowsTTY::setRawMode()
{
    // Enable virtual terminal input - disables line input, echo, processed input
    DWORD const inputMode = ENABLE_VIRTUAL_TERMINAL_INPUT;
    SetConsoleMode(_hStdin, inputMode);

    // Enable virtual terminal processing for output with newline control
    DWORD const outputMode =
        ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN;
    SetConsoleMode(_hStdout, outputMode);
}

void WindowsTTY::restoreMode()
{
    SetConsoleMode(_hStdin, static_cast<DWORD>(_originalInputMode));
    if (_originalOutputMode != 0)
        SetConsoleMode(_hStdout, static_cast<DWORD>(_originalOutputMode));
}

void WindowsTTY::setEchoEnabled(bool enabled)
{
    DWORD mode = 0;
    if (GetConsoleMode(_hStdin, &mode))
    {
        if (enabled)
            mode |= ENABLE_ECHO_INPUT;
        else
            mode &= ~ENABLE_ECHO_INPUT;
        SetConsoleMode(_hStdin, mode);
    }
}

std::optional<char> WindowsTTY::readCharWithTimeout(std::chrono::milliseconds timeout)
{
    auto const timeoutMs = (timeout.count() == 0) ? INFINITE : static_cast<DWORD>(timeout.count());

    while (true)
    {
        auto const result = WaitForSingleObject(_hStdin, timeoutMs);
        if (result != WAIT_OBJECT_0)
            return std::nullopt; // Timeout or error

        // Read console input records looking for a key event
        DWORD numEvents = 0;
        if (!GetNumberOfConsoleInputEvents(_hStdin, &numEvents) || numEvents == 0)
            return std::nullopt;

        INPUT_RECORD rec {};
        DWORD eventsRead = 0;
        if (!ReadConsoleInputW(_hStdin, &rec, 1, &eventsRead) || eventsRead == 0)
            return std::nullopt;

        if (rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown)
        {
            auto const wc = rec.Event.KeyEvent.uChar.UnicodeChar;
            if (wc != 0)
            {
                // Convert UTF-16 code unit to its first UTF-8 byte.
                // This function returns std::optional<char>, so we can only
                // return one byte — sufficient for ASCII and the lead byte of
                // multi-byte sequences (used for single-key reads like `read -n 1`).
                if (wc < 0x80)
                    return static_cast<char>(wc);
                if (wc < 0x800)
                    return static_cast<char>(0xC0 | (wc >> 6));
                return static_cast<char>(0xE0 | (wc >> 12));
            }
        }
        // Skip non-key events and key-up events, keep waiting
    }
}

void WindowsTTY::writeToStdout(std::string_view str) const
{
    DWORD written = 0;
    WriteFile(_hStdout, str.data(), static_cast<DWORD>(str.size()), &written, nullptr);
}

void WindowsTTY::writeToStdin(std::string_view str) const
{
    // Inject characters as INPUT_RECORDs into the console input buffer
    std::vector<INPUT_RECORD> records;
    records.reserve(str.size());

    for (auto const ch: str)
    {
        INPUT_RECORD rec {};
        rec.EventType = KEY_EVENT;
        rec.Event.KeyEvent.bKeyDown = TRUE;
        rec.Event.KeyEvent.wRepeatCount = 1;
        rec.Event.KeyEvent.uChar.UnicodeChar = static_cast<wchar_t>(static_cast<unsigned char>(ch));
        records.push_back(rec);
    }

    if (!records.empty())
    {
        DWORD written = 0;
        WriteConsoleInputW(_hStdin, records.data(), static_cast<DWORD>(records.size()), &written);
    }
}

} // namespace endo

#endif // _WIN32
