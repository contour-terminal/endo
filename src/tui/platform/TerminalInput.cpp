// SPDX-License-Identifier: Apache-2.0
#include <tui/TerminalInput.hpp>
#include <tui/TerminalProtocols.hpp>
#include <tui/platform/PosixIO.hpp>

#include <array>
#include <string_view>

#include <sys/ioctl.h>

#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

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
    auto fds = std::array<struct pollfd, 3> {};
    fds[0] = { .fd = _fd, .events = POLLIN, .revents = 0 };
    fds[1] = { .fd = _resizePipe[0], .events = POLLIN, .revents = 0 };
    fds[2] = { .fd = _wakeup ? _wakeup->nativeHandle() : -1, .events = POLLIN, .revents = 0 };

    auto nfds = nfds_t { 1 };
    if (_resizePipe[0] != -1)
        nfds = 2;
    if (_wakeup)
        nfds = 3;

    auto const pollResult = ::poll(fds.data(), nfds, timeoutMs);

    if (pollResult <= 0)
    {
        // Timeout or error — check for pending partial sequences
        if (pollResult == 0)
            return parserTimeout();
        return {};
    }

    auto events = std::vector<InputEvent> {};

    // Check resize pipe
    if (nfds >= 2 && (fds[1].revents & POLLIN) != 0)
        if (auto const resize = drainResize())
            events.emplace_back(*resize);

    // Check wakeup fd — just drain it; the actual messages are in the MessageQueue.
    if (nfds >= 3 && (fds[2].revents & POLLIN) != 0)
        _wakeup->reset();

    // Check stdin
    if ((fds[0].revents & POLLIN) != 0)
    {
        auto parsed = readReadyInput();
        events.insert(
            events.end(), std::make_move_iterator(parsed.begin()), std::make_move_iterator(parsed.end()));
    }

    return events;
}

auto TerminalInput::inputNativeHandle() const noexcept -> endo::platform::NativeHandle
{
    return _fd;
}

auto TerminalInput::resizeNativeHandle() const noexcept -> endo::platform::NativeHandle
{
    return _resizePipe[0];
}

auto TerminalInput::readReadyInput() -> std::vector<InputEvent>
{
    auto events = std::vector<InputEvent> {};
    auto buf = std::array<char, 512> {};
    auto const n = safeRead(_fd, buf.data(), buf.size());
    if (n > 0)
    {
        auto parsed = _parser.feed(std::string_view(buf.data(), static_cast<size_t>(n)));
        events.insert(
            events.end(), std::make_move_iterator(parsed.begin()), std::make_move_iterator(parsed.end()));
    }
    return events;
}

auto TerminalInput::drainResize() -> std::optional<ResizeEvent>
{
    // Drain the self-pipe so the next resize re-arms the notification.
    auto buf = char {};
    while (safeRead(_resizePipe[0], &buf, 1) > 0)
        ;

    auto ws = winsize {};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0)
        return ResizeEvent { .columns = ws.ws_col, .rows = ws.ws_row };
    return std::nullopt;
}

auto TerminalInput::parserTimeout() -> std::vector<InputEvent>
{
    return _parser.timeout();
}

void TerminalInput::setWakeup(endo::platform::Wakeup* wakeup)
{
    _wakeup = wakeup;
}

void TerminalInput::notifyResize(int /*cols*/, int /*rows*/)
{
    if (_resizePipe[1] != -1)
    {
        auto const byte = char { 1 };
        safeWrite(_resizePipe[1], &byte, 1);
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

void TerminalInput::writeProtocol(std::string_view data) const
{
    safeWrite(_outFd, data.data(), data.size());
}

void TerminalInput::setAnyMotionTracking(bool enabled)
{
    _anyMotionTracking = enabled;
    if (_rawMode)
        writeProtocol(enabled ? protocols::EnableAnyMotionTracking : protocols::DisableAnyMotionTracking);
}

void TerminalInput::enableProtocols()
{
    writeProtocol(protocols::EnableCsiU);
    writeProtocol(
        protocols::EnablePassiveMouseTracking); // Implicitly enables SGR (1006) + button tracking (1002)
    if (_anyMotionTracking)
        writeProtocol(protocols::EnableAnyMotionTracking);
    writeProtocol(protocols::EnableBracketedPaste);
    writeProtocol(protocols::EnableColorSchemeNotify);
    writeProtocol(protocols::QueryColorScheme);
    writeProtocol(protocols::EnableFocusTracking);
}

void TerminalInput::disableProtocols()
{
    writeProtocol(protocols::DisableFocusTracking);
    writeProtocol(protocols::DisableColorSchemeNotify);
    writeProtocol(protocols::DisableBracketedPaste);
    if (_anyMotionTracking)
        writeProtocol(protocols::DisableAnyMotionTracking);
    writeProtocol(protocols::DisablePassiveMouseTracking); // Also clears implicit mouse modes
    writeProtocol(protocols::DisableCsiU);
}

} // namespace tui
