// SPDX-License-Identifier: Apache-2.0
#include <tui/TerminalInput.hpp>
#include <tui/TerminalProtocols.hpp>

#if defined(_WIN32)

    #include <tui/platform/Win32Utf.hpp>

    #include <array>
    #include <string_view>

    #include <windows.h>

    #include <platform/Wakeup.hpp>

namespace tui
{

TerminalInput::TerminalInput() = default;

TerminalInput::~TerminalInput()
{
    shutdown();
}

auto TerminalInput::initialize() -> VoidResult
{
    _hStdin = GetStdHandle(STD_INPUT_HANDLE);
    _hStdout = GetStdHandle(STD_OUTPUT_HANDLE);

    if (_hStdin == INVALID_HANDLE_VALUE || _hStdout == INVALID_HANDLE_VALUE)
        return makeError(ErrorCode::IoError, "Failed to get console handles");

    // Save original console modes
    if (!GetConsoleMode(_hStdin, &_originalInputMode))
        return makeError(ErrorCode::IoError, "Failed to get console input mode");
    if (!GetConsoleMode(_hStdout, &_originalOutputMode))
        return makeError(ErrorCode::IoError, "Failed to get console output mode");

    // Create a manual-reset event for resize notification
    _resizeEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (_resizeEvent == nullptr)
        return makeError(ErrorCode::IoError, "Failed to create resize event");

    enableRawMode();
    enableProtocols();

    return {};
}

void TerminalInput::shutdown()
{
    if (_rawMode)
    {
        disableProtocols();
        disableRawMode();
    }

    if (_resizeEvent != nullptr)
    {
        CloseHandle(_resizeEvent);
        _resizeEvent = nullptr;
    }
}

auto TerminalInput::poll(int timeoutMs) -> std::vector<InputEvent>
{
    auto const timeout = (timeoutMs < 0) ? INFINITE : static_cast<DWORD>(timeoutMs);

    // Wait on both stdin and the resize event
    HANDLE handles[2] = { _hStdin, _resizeEvent };
    auto const handleCount = (_resizeEvent != nullptr) ? 2u : 1u;

    auto const waitResult = WaitForMultipleObjects(handleCount, handles, FALSE, timeout);

    if (waitResult == WAIT_TIMEOUT)
        return parserTimeout();

    if (waitResult == WAIT_FAILED)
        return {};

    auto events = std::vector<InputEvent> {};

    if (auto const resize = drainResize())
        events.emplace_back(*resize);

    auto parsed = readReadyInput();
    events.insert(
        events.end(), std::make_move_iterator(parsed.begin()), std::make_move_iterator(parsed.end()));

    return events;
}

auto TerminalInput::inputNativeHandle() const noexcept -> endo::platform::NativeHandle
{
    return _hStdin;
}

auto TerminalInput::resizeNativeHandle() const noexcept -> endo::platform::NativeHandle
{
    return _resizeEvent;
}

auto TerminalInput::drainResize() -> std::optional<ResizeEvent>
{
    if (_resizeEvent == nullptr || WaitForSingleObject(_resizeEvent, 0) != WAIT_OBJECT_0)
        return std::nullopt;

    ResetEvent(_resizeEvent);

    CONSOLE_SCREEN_BUFFER_INFO csbi {};
    if (GetConsoleScreenBufferInfo(_hStdout, &csbi))
    {
        auto const cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        auto const rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        return ResizeEvent { .columns = static_cast<int>(cols), .rows = static_cast<int>(rows) };
    }
    return std::nullopt;
}

auto TerminalInput::readReadyInput() -> std::vector<InputEvent>
{
    auto events = std::vector<InputEvent> {};

    // Process console input records
    DWORD numEvents = 0;
    if (!GetNumberOfConsoleInputEvents(_hStdin, &numEvents) || numEvents == 0)
        return events;

    auto inputRecords = std::vector<INPUT_RECORD>(numEvents);
    DWORD eventsRead = 0;
    if (!ReadConsoleInput(_hStdin, inputRecords.data(), numEvents, &eventsRead))
        return events;

    // Accumulate character data from KEY_EVENT records, then feed to VT parser.
    // With ENABLE_VIRTUAL_TERMINAL_INPUT, Windows Terminal sends CSI escape sequences
    // identical to Linux terminals, so the VtParser pipeline works unchanged.
    auto vtData = std::string {};
    for (DWORD i = 0; i < eventsRead; ++i)
    {
        auto const& rec = inputRecords[i];

        if (rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown)
        {
            auto const wc = rec.Event.KeyEvent.uChar.UnicodeChar;
            if (wc != 0)
                appendUtf16AsUtf8(vtData, wc);
        }
        else if (rec.EventType == WINDOW_BUFFER_SIZE_EVENT)
        {
            // Also handle resize events from input records
            CONSOLE_SCREEN_BUFFER_INFO csbi {};
            if (GetConsoleScreenBufferInfo(_hStdout, &csbi))
            {
                auto const cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
                auto const rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
                events.emplace_back(
                    ResizeEvent { .columns = static_cast<int>(cols), .rows = static_cast<int>(rows) });
            }
        }
    }

    // Feed accumulated VT data to the parser
    if (!vtData.empty())
    {
        auto parsed = _parser.feed(vtData);
        events.insert(
            events.end(), std::make_move_iterator(parsed.begin()), std::make_move_iterator(parsed.end()));
    }

    return events;
}

auto TerminalInput::parserTimeout() -> std::vector<InputEvent>
{
    return _parser.timeout();
}

void TerminalInput::notifyResize(int /*cols*/, int /*rows*/)
{
    if (_resizeEvent != nullptr)
        SetEvent(_resizeEvent);
}

auto TerminalInput::resizePipeReadFd() const noexcept -> int
{
    return -1; // Not applicable on Windows
}

void TerminalInput::suspend()
{
    if (_suspended || !_rawMode)
        return;

    disableProtocols();
    disableRawMode();
    _suspended = true;
}

void TerminalInput::resume()
{
    if (!_suspended)
        return;

    enableRawMode();
    enableProtocols();
    _suspended = false;
}

auto TerminalInput::isSuspended() const noexcept -> bool
{
    return _suspended;
}

void TerminalInput::setWakeup(endo::platform::Wakeup* wakeup)
{
    _wakeup = wakeup;
}

void TerminalInput::enableRawMode()
{
    // Enable VT input processing, disable line input, echo, and processed input.
    // With ENABLE_VIRTUAL_TERMINAL_INPUT, the console sends CSI escape sequences
    // for special keys, matching the behavior of Linux terminals.
    DWORD const inputMode = ENABLE_VIRTUAL_TERMINAL_INPUT;
    SetConsoleMode(_hStdin, inputMode);

    // Enable VT output processing and disable automatic newline translation
    DWORD const outputMode =
        ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN;
    SetConsoleMode(_hStdout, outputMode);

    _rawMode = true;
}

void TerminalInput::disableRawMode()
{
    if (_rawMode)
    {
        SetConsoleMode(_hStdin, _originalInputMode);
        SetConsoleMode(_hStdout, _originalOutputMode);
        _rawMode = false;
    }
}

void TerminalInput::writeProtocol(std::string_view data) const
{
    DWORD written = 0;
    WriteFile(_hStdout, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
}

void TerminalInput::setAnyMotionTracking(bool enabled)
{
    _anyMotionTracking = enabled;
    if (_rawMode)
        writeProtocol(enabled ? protocols::EnableAnyMotionTracking : protocols::DisableAnyMotionTracking);
}

void TerminalInput::enableProtocols()
{
    writeProtocol(protocols::EnableWin32InputMode);
    writeProtocol(protocols::EnableCsiU);
    writeProtocol(protocols::EnablePassiveMouseTracking);
    if (_anyMotionTracking)
        writeProtocol(protocols::EnableAnyMotionTracking);
    writeProtocol(protocols::EnableBracketedPaste);
    writeProtocol(protocols::EnableColorSchemeNotify);
    writeProtocol(protocols::QueryColorScheme);
}

void TerminalInput::disableProtocols()
{
    writeProtocol(protocols::DisableColorSchemeNotify);
    writeProtocol(protocols::DisableBracketedPaste);
    if (_anyMotionTracking)
        writeProtocol(protocols::DisableAnyMotionTracking);
    writeProtocol(protocols::DisablePassiveMouseTracking);
    writeProtocol(protocols::DisableWin32InputMode);
    writeProtocol(protocols::DisableCsiU);
}

} // namespace tui

#endif // _WIN32
