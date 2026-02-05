// SPDX-License-Identifier: Apache-2.0
#include <array>
#include <string_view>

#include <sys/ioctl.h>

#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include <tui/TerminalInput.hpp>

namespace tui
{

namespace
{
    using namespace std::string_view_literals;

    // Terminal protocol escape sequences
    //
    // Kitty keyboard protocol flags (CSI > flags u):
    //   1 = Disambiguate escape codes
    //   2 = Report event types (press, repeat, release)
    //   4 = Report alternate keys
    //   8 = Report all keys as escape codes
    //  16 = Report associated text
    //
    // We use flags 1|8=9 to ensure modifiers are reported for all keys including Enter.
    // Flag 8 is needed because some terminals only report Shift+Enter with this flag.
    constexpr auto EnableCsiU = "\033[>9u"sv;             // Kitty keyboard protocol (disambiguate + all keys)
    constexpr auto DisableCsiU = "\033[<u"sv;             // Pop Kitty keyboard protocol
    constexpr auto EnableSgrMouse = "\033[?1006h"sv;      // SGR mouse mode
    constexpr auto DisableSgrMouse = "\033[?1006l"sv;     // Disable SGR mouse mode
    constexpr auto EnableMouseTracking = "\033[?1000h"sv; // Basic mouse tracking
    constexpr auto DisableMouseTracking = "\033[?1000l"sv;
    constexpr auto EnableBracketedPaste = "\033[?2004h"sv;
    constexpr auto DisableBracketedPaste = "\033[?2004l"sv;

    /// Writes a string_view to the terminal (stdout).
    void writeToTerminal(std::string_view data)
    {
        static_cast<void>(write(STDOUT_FILENO, data.data(), data.size()));
    }
} // namespace

TerminalInput::TerminalInput() = default;

TerminalInput::~TerminalInput()
{
    shutdown();
}

auto TerminalInput::initialize() -> VoidResult
{
    _fd = STDIN_FILENO;

    // Create self-pipe for SIGWINCH notification
    if (pipe(_resizePipe) == -1)
        return makeError(ErrorCode::IoError, "Failed to create resize notification pipe");

    // Make read end non-blocking
    auto const flags = fcntl(_resizePipe[0], F_GETFL, 0);
    fcntl(_resizePipe[0], F_SETFL, flags | O_NONBLOCK);

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

    if (_resizePipe[0] != -1)
    {
        close(_resizePipe[0]);
        close(_resizePipe[1]);
        _resizePipe[0] = -1;
        _resizePipe[1] = -1;
    }
}

auto TerminalInput::poll(int timeoutMs) -> std::vector<InputEvent>
{
    auto fds = std::array<struct pollfd, 2> {};
    fds[0] = { .fd = _fd, .events = POLLIN, .revents = 0 };
    fds[1] = { .fd = _resizePipe[0], .events = POLLIN, .revents = 0 };

    auto const nfds = (_resizePipe[0] != -1) ? 2 : 1;
    auto const pollResult = ::poll(fds.data(), static_cast<nfds_t>(nfds), timeoutMs);

    if (pollResult <= 0)
    {
        // Timeout or error — check for pending partial sequences
        if (pollResult == 0)
            return _parser.timeout();
        return {};
    }

    auto events = std::vector<InputEvent> {};

    // Check resize pipe
    if (nfds >= 2 && (fds[1].revents & POLLIN) != 0)
    {
        // Drain the pipe
        auto buf = char {};
        while (read(_resizePipe[0], &buf, 1) > 0)
            ;

        // Query actual terminal size
        auto ws = winsize {};
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0)
            events.emplace_back(ResizeEvent { .columns = ws.ws_col, .rows = ws.ws_row });
    }

    // Check stdin
    if ((fds[0].revents & POLLIN) != 0)
    {
        auto buf = std::array<char, 512> {};
        auto const n = read(_fd, buf.data(), buf.size());
        if (n > 0)
        {
            auto parsed = _parser.feed(std::string_view(buf.data(), static_cast<size_t>(n)));
            events.insert(
                events.end(), std::make_move_iterator(parsed.begin()), std::make_move_iterator(parsed.end()));
        }
    }

    return events;
}

void TerminalInput::notifyResize(int /*cols*/, int /*rows*/)
{
    if (_resizePipe[1] != -1)
    {
        auto const byte = char { 1 };
        auto const result = write(_resizePipe[1], &byte, 1);
        static_cast<void>(result);
    }
}

auto TerminalInput::resizePipeReadFd() const noexcept -> int
{
    return _resizePipe[0];
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

void TerminalInput::enableRawMode()
{
    tcgetattr(_fd, &_origTermios);
    auto raw = _origTermios;
    raw.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(_fd, TCSAFLUSH, &raw);
    _rawMode = true;
}

void TerminalInput::disableRawMode()
{
    if (_rawMode)
    {
        tcsetattr(_fd, TCSAFLUSH, &_origTermios);
        _rawMode = false;
    }
}

void TerminalInput::enableProtocols()
{
    writeToTerminal(EnableCsiU);
    writeToTerminal(EnableMouseTracking);
    writeToTerminal(EnableSgrMouse);
    writeToTerminal(EnableBracketedPaste);
}

void TerminalInput::disableProtocols()
{
    writeToTerminal(DisableBracketedPaste);
    writeToTerminal(DisableSgrMouse);
    writeToTerminal(DisableMouseTracking);
    writeToTerminal(DisableCsiU);
}

} // namespace tui
