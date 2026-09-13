// SPDX-License-Identifier: Apache-2.0
//
// The Windows console input arm, against a real console: size changes, and replies to terminal
// queries arriving as console input. Compiles to nothing elsewhere -- the POSIX arm reports a size
// change through SIGWINCH and reads replies from a file descriptor, and TerminalQuery_test drives
// the reply loop both arms share through a scripted input.

#if defined(_WIN32)

    #include <tui/InputEvent.hpp>
    #include <tui/MockTerminalOutput.hpp>
    #include <tui/Terminal.hpp>
    #include <tui/TerminalInput.hpp>
    #include <tui/TerminalOutput.hpp>

    #include <catch2/catch_test_macros.hpp>

    #include <chrono>
    #include <memory>
    #include <optional>
    #include <string_view>
    #include <tuple>
    #include <variant>
    #include <vector>

    #include <platform/Clock.hpp>
    #include <windows.h>

using tui::DecModeStatus;
using tui::InputEvent;
using tui::MockTerminalOutput;
using tui::ResizeEvent;
using tui::Terminal;
using tui::TerminalInput;
using tui::TerminalOutput;
using tui::TerminalQueryInput;

namespace
{

/// This process's console, as the standard handles, for the length of one case.
///
/// `CONIN$` and `CONOUT$` name the console the process is attached to whatever its standard handles
/// were redirected to -- a test runner hands a test pipes. A process with no console at all cannot
/// open them, and the case says so rather than passing.
///
/// A console is shared by every process attached to it, and a runner that gives each case its own
/// process runs them side by side on ONE console: one case's poll would read the reply another typed,
/// and one case's flush would discard it. So the handles are taken under a named mutex, held for the
/// case, and a case that cannot get it within the bound fails rather than racing.
class ConsoleStandardHandles
{
  public:
    ConsoleStandardHandles():
        _lock { CreateMutexW(nullptr, FALSE, L"Local\\endo-tui-console-input-test") },
        _locked { _lock != nullptr && isAcquired(WaitForSingleObject(_lock, LockBoundMs)) },
        _savedInput { GetStdHandle(STD_INPUT_HANDLE) },
        _savedOutput { GetStdHandle(STD_OUTPUT_HANDLE) },
        _input { CreateFileW(
            L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr) },
        _output { CreateFileW(
            L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr) }
    {
        if (available())
        {
            SetStdHandle(STD_INPUT_HANDLE, _input);
            SetStdHandle(STD_OUTPUT_HANDLE, _output);
        }
    }

    ~ConsoleStandardHandles()
    {
        SetStdHandle(STD_INPUT_HANDLE, _savedInput);
        SetStdHandle(STD_OUTPUT_HANDLE, _savedOutput);
        if (_input != INVALID_HANDLE_VALUE)
            CloseHandle(_input);
        if (_output != INVALID_HANDLE_VALUE)
            CloseHandle(_output);
        if (_locked)
            ReleaseMutex(_lock);
        if (_lock != nullptr)
            CloseHandle(_lock);
    }

    ConsoleStandardHandles(ConsoleStandardHandles const&) = delete;
    ConsoleStandardHandles& operator=(ConsoleStandardHandles const&) = delete;
    ConsoleStandardHandles(ConsoleStandardHandles&&) = delete;
    ConsoleStandardHandles& operator=(ConsoleStandardHandles&&) = delete;

    /// @return Whether this case holds the console for itself. Checked before anything else is.
    [[nodiscard]] bool locked() const noexcept { return _locked; }

    [[nodiscard]] bool available() const noexcept
    {
        return _input != INVALID_HANDLE_VALUE && _output != INVALID_HANDLE_VALUE;
    }

    [[nodiscard]] HANDLE input() const noexcept { return _input; }
    [[nodiscard]] HANDLE output() const noexcept { return _output; }

  private:
    /// How long a case waits for another to finish with the console. Each case is well under a
    /// second or two, so this is a wedged holder rather than a queue.
    static constexpr DWORD LockBoundMs = 30000;

    /// An abandoned mutex -- its holder died mid-case -- is still acquired, and the console state
    /// that holder left is what the flush in each case exists for.
    [[nodiscard]] static bool isAcquired(DWORD waited) noexcept
    {
        return waited == WAIT_OBJECT_0 || waited == WAIT_ABANDONED;
    }

    HANDLE _lock;
    bool _locked;
    HANDLE _savedInput;
    HANDLE _savedOutput;
    HANDLE _input;
    HANDLE _output;
};

/// @return The console window's size in cells, as `GetConsoleScreenBufferInfo` reports it.
[[nodiscard]] std::optional<ResizeEvent> windowSize(HANDLE output)
{
    auto info = CONSOLE_SCREEN_BUFFER_INFO {};
    if (GetConsoleScreenBufferInfo(output, &info) == 0)
        return std::nullopt;
    return ResizeEvent { .columns = info.srWindow.Right - info.srWindow.Left + 1,
                         .rows = info.srWindow.Bottom - info.srWindow.Top + 1 };
}

/// The real console input as the query loop's input, so a reply is read from the console.
///
/// Paired with a mock OUTPUT, so no query reaches the console: a console that answers DECRQM itself
/// would race the reply this file writes, and the case would read whichever arrived first.
class ConsoleQueryInput final: public TerminalQueryInput
{
  public:
    explicit ConsoleQueryInput(TerminalInput& input): _input { input } {}

    [[nodiscard]] auto poll(int timeoutMs) -> std::vector<InputEvent> override { return _input.poll(timeoutMs); }
    void unread(std::vector<InputEvent> events) override { _input.unread(std::move(events)); }

  private:
    TerminalInput& _input;
};

/// Write @p text to the console, as a terminal sees an application's output.
/// @return Whether all of it was written.
[[nodiscard]] bool writeToConsole(HANDLE output, std::string_view text)
{
    auto written = DWORD { 0 };
    return WriteFile(output, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) != 0
           && written == text.size();
}

/// Whether the console host answers a DECRQM at all, asked of its input buffer rather than of the code
/// under test: a regression that loses replies must fail the case below, not read as a silent host.
///
/// Asks about a mode the case does not query, so a reply arriving after the flush cannot be taken for
/// the answer the case is waiting on.
/// @return Whether any input arrived within the bound after the query was written.
[[nodiscard]] bool hostAnswersDecModeQueries(HANDLE input, HANDLE output)
{
    constexpr auto Bound = std::chrono::milliseconds(1000);
    FlushConsoleInputBuffer(input);
    if (!writeToConsole(output, "\x1b[?7$p"))
        return false;
    auto const deadline = std::chrono::steady_clock::now() + Bound;
    auto pending = DWORD { 0 };
    while (GetNumberOfConsoleInputEvents(input, &pending) != 0 && pending == 0)
    {
        auto const now = std::chrono::steady_clock::now();
        if (now >= deadline)
            break;
        auto const remaining = std::chrono::ceil<std::chrono::milliseconds>(deadline - now).count();
        WaitForSingleObject(input, static_cast<DWORD>(remaining));
    }
    // Let the rest of the reply land before it is discarded, so none of it reaches the case.
    Sleep(100);
    FlushConsoleInputBuffer(input);
    return pending != 0;
}

/// @return The last resize among @p events, if any.
[[nodiscard]] std::optional<ResizeEvent> lastResize(std::vector<InputEvent> const& events)
{
    auto found = std::optional<ResizeEvent> {};
    for (auto const& event: events)
        if (auto const* resize = std::get_if<ResizeEvent>(&event))
            found = *resize;
    return found;
}

} // namespace

TEST_CASE("On Windows raw-mode input asks the console to report size changes", "[TerminalInput]")
{
    // Asserted as the flag, because no case here can see it missing: an injected size record reaches
    // the input buffer whether or not it is set, and so -- measured on Windows 11 conhost and Windows
    // Terminal -- does a real resize. The flag is the documented condition, for a host that keeps to it.
    auto const console = ConsoleStandardHandles {};
    REQUIRE(console.locked());
    if (!console.available())
        SKIP("this process is attached to no console, so CONIN$ cannot be opened and no console mode exists to read");

    auto input = TerminalInput {};
    REQUIRE(input.initialize());
    auto mode = DWORD { 0 };
    auto const read = GetConsoleMode(console.input(), &mode) != 0;
    input.shutdown();

    REQUIRE(read);
    CHECK((mode & ENABLE_WINDOW_INPUT) != 0);
    CHECK((mode & ENABLE_VIRTUAL_TERMINAL_INPUT) != 0);
}

TEST_CASE("On Windows a console size record arrives as a resize carrying the window's geometry", "[TerminalInput]")
{
    auto const console = ConsoleStandardHandles {};
    REQUIRE(console.locked());
    if (!console.available())
        SKIP("this process is attached to no console, so CONIN$ cannot be opened to write a size record into");

    auto input = TerminalInput {};
    REQUIRE(input.initialize());
    // Whatever the attached console already holds -- keys typed at it -- is not this case's input.
    FlushConsoleInputBuffer(console.input());

    auto record = INPUT_RECORD {};
    record.EventType = WINDOW_BUFFER_SIZE_EVENT;
    record.Event.WindowBufferSizeEvent.dwSize = COORD { .X = 123, .Y = 45 };
    auto written = DWORD { 0 };
    auto const wrote = WriteConsoleInputW(console.input(), &record, 1, &written) != 0 && written == 1;
    auto const events = wrote ? input.poll(1000) : std::vector<InputEvent> {};
    input.shutdown();

    REQUIRE(wrote);
    auto const expected = windowSize(console.output());
    REQUIRE(expected.has_value());
    auto const resize = lastResize(events);
    REQUIRE(resize.has_value());
    // The WINDOW's size, read when the record is processed, not the buffer size the record carries:
    // the buffer can be taller than what is drawn, and a dashboard sized to it would draw off-screen.
    CHECK(resize->columns == expected->columns);
    CHECK(resize->rows == expected->rows);
}

TEST_CASE("On Windows resizing the console buffer is reported to raw-mode input", "[TerminalInput]")
{
    // End to end: the console, not the test, produces the size record, and this arm turns it into a
    // resize. A console that refuses to resize its buffer -- the one this suite gets under Git Bash
    // does -- cannot be made to produce one, and the case says so rather than passing.
    auto const console = ConsoleStandardHandles {};
    REQUIRE(console.locked());
    if (!console.available())
        SKIP("this process is attached to no console, so CONIN$ cannot be opened");

    auto before = CONSOLE_SCREEN_BUFFER_INFO {};
    REQUIRE(GetConsoleScreenBufferInfo(console.output(), &before) != 0);

    auto input = TerminalInput {};
    REQUIRE(input.initialize());
    FlushConsoleInputBuffer(console.input());

    auto grown = before.dwSize;
    grown.Y = static_cast<SHORT>(grown.Y + 1);
    if (SetConsoleScreenBufferSize(console.output(), grown) == 0)
    {
        input.shutdown();
        SKIP("this console refused a buffer resize, so it cannot be made to report one");
    }
    auto const events = input.poll(1000);
    SetConsoleScreenBufferSize(console.output(), before.dwSize);
    input.shutdown();

    CHECK(lastResize(events).has_value());
}

TEST_CASE("On Windows the console's DECRQM answer reaches the query as Set and then Reset", "[TerminalInput]")
{
    // The reply loop both arms share, fed by the Windows console input, with raw mode and every protocol
    // this arm enables switched on. The CONSOLE answers, which is the path a terminal's reply takes.
    //
    // Typing the reply in would not be that path. Measured: a console encodes injected key records as
    // win32-input-mode sequences for a client that enabled mode 9001 -- eleven typed characters became
    // 166 records and decoded to key presses, with the ESC dropped -- while the host's own replies arrive
    // as text (conhost answered DECRQM and DA1 through this code, and so did Windows Terminal).
    auto const console = ConsoleStandardHandles {};
    REQUIRE(console.locked());
    if (!console.available())
        SKIP("this process is attached to no console, so CONIN$ cannot be opened to read a reply from");

    auto input = TerminalInput {};
    REQUIRE(input.initialize());
    if (!hostAnswersDecModeQueries(console.input(), console.output()))
    {
        input.shutdown();
        SKIP("this console host put nothing in the input buffer within 1 s of a DECRQM, so it answers none "
             "and there is no reply to read");
    }
    auto queryInput = ConsoleQueryInput { input };
    auto terminal = Terminal { std::make_unique<TerminalOutput>(), queryInput, endo::platform::defaultSteadyClock() };

    // DECTCEM, the cursor's visibility: a mode every console host implements and a case can switch.
    constexpr auto CursorVisible = 25;
    auto const shown = writeToConsole(console.output(), "\x1b[?25h");
    auto const whileShown = terminal.queryDecMode(CursorVisible);
    auto const hidden = writeToConsole(console.output(), "\x1b[?25l");
    auto const whileHidden = terminal.queryDecMode(CursorVisible);
    std::ignore = writeToConsole(console.output(), "\x1b[?25h");
    input.shutdown();

    REQUIRE(shown);
    REQUIRE(hidden);
    CHECK(whileShown == DecModeStatus::Set);
    CHECK(whileHidden == DecModeStatus::Reset);
}

TEST_CASE("On Windows a DECRQM that nothing answers is NoReply at the deadline, not a hang", "[TerminalInput]")
{
    auto const console = ConsoleStandardHandles {};
    REQUIRE(console.locked());
    if (!console.available())
        SKIP("this process is attached to no console, so CONIN$ cannot be opened to wait on");

    auto input = TerminalInput {};
    REQUIRE(input.initialize());
    FlushConsoleInputBuffer(console.input());
    auto queryInput = ConsoleQueryInput { input };
    auto terminal = Terminal { std::make_unique<MockTerminalOutput>(), queryInput, endo::platform::defaultSteadyClock() };

    auto const answer = terminal.queryDecMode(2026);
    input.shutdown();

    CHECK(answer == DecModeStatus::NoReply);
}

#endif
