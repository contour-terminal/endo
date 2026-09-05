// SPDX-License-Identifier: Apache-2.0
#include "TTY.hpp"

#include <chrono>
#include <cstring>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>

#if !defined(_WIN32)
    #include <sys/ioctl.h>

    #include <fcntl.h>
    #include <poll.h>
    #include <unistd.h>
    #if defined(__APPLE__)
        #include <util.h>
    #else
        #include <pty.h>
    #endif
#endif

namespace endo
{

#if !defined(_WIN32)

void setRawMode(NativeHandle fd)
{
    auto tio = termios {};
    tcgetattr(fd, &tio);

    tio.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    tio.c_oflag &= ~(OPOST);
    tio.c_cflag |= (CS8);
    tio.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 1;

    int const result = tcsetattr(STDIN_FILENO, TCSAFLUSH, &tio);
    if (result == -1)
        throw std::runtime_error("tcsetattr: " + std::string(strerror(errno)));
}

void safeClose(NativeHandle* fd) noexcept
{
    if (fd && *fd != InvalidHandle)
    {
        ::close(*fd);
        *fd = InvalidHandle;
    }
}

RealTTY::RealTTY()
{
    _hasTTY = isatty(STDIN_FILENO) == 1;
    if (_hasTTY)
    {
        // Save original terminal attributes
        if (tcgetattr(STDIN_FILENO, &_originalTermios) != 0)
            _hasTTY = false; // Not a real TTY if we can't get attributes
    }
}

RealTTY::~RealTTY()
{
    if (_hasTTY)
        restoreMode();
}

RealTTY& RealTTY::instance()
{
    static RealTTY instance;
    return instance;
}

NativeHandle RealTTY::inputFd() const noexcept
{
    return STDIN_FILENO;
}

NativeHandle RealTTY::outputFd() const noexcept
{
    return STDOUT_FILENO;
}

bool RealTTY::isTerminal() const noexcept
{
    return _hasTTY;
}

std::expected<TerminalSize, ShellError> RealTTY::getSize() const
{
    winsize ws {};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1)
        return std::unexpected(ShellError::IoError);
    return TerminalSize {
        .rows = ws.ws_row, .cols = ws.ws_col, .xpixel = ws.ws_xpixel, .ypixel = ws.ws_ypixel
    };
}

void RealTTY::setRawMode()
{
    if (_hasTTY)
        endo::setRawMode(STDIN_FILENO);
}

void RealTTY::restoreMode()
{
    if (!_hasTTY)
        return;

    int const result = tcsetattr(STDIN_FILENO, TCSAFLUSH, &_originalTermios);
    if (result == -1)
        throw std::runtime_error("tcsetattr: " + std::string(strerror(errno)));
}

void RealTTY::setEchoEnabled(bool enabled)
{
    if (!_hasTTY)
        return;

    auto tio = termios {};
    if (tcgetattr(STDIN_FILENO, &tio) == -1)
        return;

    if (enabled)
        tio.c_lflag |= ECHO;
    else
        tio.c_lflag &= ~static_cast<tcflag_t>(ECHO);

    tcsetattr(STDIN_FILENO, TCSANOW, &tio);
}

std::optional<char> RealTTY::readCharWithTimeout(std::chrono::milliseconds timeout)
{
    if (timeout.count() > 0)
    {
        pollfd pfd { .fd = STDIN_FILENO, .events = POLLIN, .revents = 0 };
        int const result = poll(&pfd, 1, static_cast<int>(timeout.count()));
        if (result <= 0)
            return std::nullopt; // timeout or error
    }

    auto ch = char {};
    ssize_t const n = ::read(STDIN_FILENO, &ch, 1);
    if (n <= 0)
        return std::nullopt; // EOF or error
    return ch;
}

void RealTTY::writeToStdout(std::string_view str) const
{
    ssize_t const result = ::write(STDOUT_FILENO, str.data(), str.size());
    if (result == -1)
        throw std::runtime_error("write: " + std::string(strerror(errno)));
}

void RealTTY::writeToStderr(std::string_view str) const
{
    ssize_t const result = ::write(STDERR_FILENO, str.data(), str.size());
    if (result == -1)
        throw std::runtime_error("write: " + std::string(strerror(errno)));
}

bool RealTTY::isStderrTerminal() const noexcept
{
    return isatty(STDERR_FILENO) == 1;
}

void RealTTY::writeToStdin(std::string_view str) const
{
    ssize_t const result = ::write(STDIN_FILENO, str.data(), str.size());
    if (result == -1)
        throw std::runtime_error("write: " + std::string(strerror(errno)));
}

// TestPTY implementation

TestPTY::TestPTY(): _windowSize { .ws_row = 25, .ws_col = 80, .ws_xpixel = 0, .ws_ypixel = 0 }
{
    char name[256] {};
    if (openpty(&_ptyMaster, &_ptySlave, name, &_baseTermios, &_windowSize) == -1)
        throw std::runtime_error("openpty: " + std::string(strerror(errno)));

    // The reader polls for readability before every read, so a read should never block --
    // O_NONBLOCK makes that a property of the descriptor rather than an assumption about
    // what poll() reported a moment earlier.
    if (auto const flags = fcntl(_ptyMaster, F_GETFL, 0); flags != -1)
        (void) fcntl(_ptyMaster, F_SETFL, flags | O_NONBLOCK);

    _updateThread = std::thread { std::bind(&TestPTY::outputUpdateLoop, this) };
}

TestPTY::~TestPTY()
{
    _closed = true;

    // Closing the slave makes the reader's poll() report POLLHUP, so it leaves its loop on
    // its own within one 50ms timeout at worst. pthread_cancel() was doing this before, but
    // it can fire while the reader holds _outputMutex, leaving it locked forever, and
    // unwinding a C++ thread through cancellation is not something the standard promises.
    safeClose(&_ptySlave);
    if (_updateThread.joinable())
        _updateThread.join();
    safeClose(&_ptyMaster);
}

NativeHandle TestPTY::inputFd() const noexcept
{
    return _ptySlave;
}

NativeHandle TestPTY::outputFd() const noexcept
{
    return _ptySlave;
}

bool TestPTY::isTerminal() const noexcept
{
    return isatty(_ptySlave) != 0;
}

std::expected<TerminalSize, ShellError> TestPTY::getSize() const
{
    return TerminalSize { .rows = _windowSize.ws_row,
                          .cols = _windowSize.ws_col,
                          .xpixel = _windowSize.ws_xpixel,
                          .ypixel = _windowSize.ws_ypixel };
}

void TestPTY::setRawMode()
{
    endo::setRawMode(STDIN_FILENO);
}

void TestPTY::restoreMode()
{
    int const result = tcsetattr(STDIN_FILENO, TCSAFLUSH, &_baseTermios);
    if (result == -1)
        throw std::runtime_error("tcsetattr: " + std::string(strerror(errno)));
}

void TestPTY::setEchoEnabled(bool enabled)
{
    auto tio = termios {};
    if (tcgetattr(_ptySlave, &tio) == -1)
        return;

    if (enabled)
        tio.c_lflag |= ECHO;
    else
        tio.c_lflag &= ~static_cast<tcflag_t>(ECHO);

    tcsetattr(_ptySlave, TCSANOW, &tio);
}

std::optional<char> TestPTY::readCharWithTimeout(std::chrono::milliseconds timeout)
{
    if (timeout.count() > 0)
    {
        pollfd pfd { .fd = _ptySlave, .events = POLLIN, .revents = 0 };
        int const result = poll(&pfd, 1, static_cast<int>(timeout.count()));
        if (result <= 0)
            return std::nullopt; // timeout or error
    }

    auto ch = char {};
    ssize_t const n = ::read(_ptySlave, &ch, 1);
    if (n <= 0)
        return std::nullopt; // EOF or error
    return ch;
}

void TestPTY::writeToStdout(std::string_view str) const
{
    ssize_t const result = ::write(_ptySlave, str.data(), str.size());
    if (result == -1)
        throw std::runtime_error("write: " + std::string(strerror(errno)));
}

void TestPTY::writeToStderr(std::string_view str) const
{
    // In test mode, stderr output goes to the PTY to be captured alongside stdout.
    ssize_t const result = ::write(_ptySlave, str.data(), str.size());
    if (result == -1)
        throw std::runtime_error("write: " + std::string(strerror(errno)));
}

bool TestPTY::isStderrTerminal() const noexcept
{
    return false;
}

void TestPTY::writeToStdin(std::string_view str) const
{
    ssize_t const result = ::write(_ptyMaster, str.data(), str.size());
    if (result == -1)
        throw std::runtime_error("write: " + std::string(strerror(errno)));
}

void TestPTY::setSize(uint16_t rows, uint16_t cols)
{
    _windowSize.ws_row = rows;
    _windowSize.ws_col = cols;
    ioctl(_ptySlave, TIOCSWINSZ, &_windowSize);
}

namespace
{
    /// @brief Reports whether @p fd has data ready to read, without consuming it.
    [[nodiscard]] bool hasPendingInput(NativeHandle fd) noexcept
    {
        auto descriptor = pollfd { .fd = fd, .events = POLLIN, .revents = 0 };
        return poll(&descriptor, 1, 0) > 0 && (descriptor.revents & POLLIN) != 0;
    }
} // namespace

std::string TestPTY::output() const
{
    // The caller has just finished a command, so everything it produced is already in the
    // PTY and nothing new can arrive. Every byte is therefore in exactly one of three
    // states, and this waits out the first two:
    //
    //   pending in the PTY          -- hasPendingInput() sees it
    //   taken but not yet appended  -- _transferring covers read()..append()
    //   appended to _output         -- done
    //
    // Checking only the PTY would miss the middle state, which is what made captures come
    // back empty. The deadline is a safety net for a wedged reader, not the mechanism.
    auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

    while (hasPendingInput(_ptyMaster) || _transferring.load(std::memory_order_acquire))
    {
        if (!_readerRunning.load(std::memory_order_acquire) || _closed.load(std::memory_order_acquire)
            || std::chrono::steady_clock::now() >= deadline)
            break;

        std::this_thread::yield();
    }

    auto _ = std::scoped_lock { _outputMutex };
    return _output;
}

void TestPTY::outputUpdateLoop()
{
    while (!_closed)
    {
        // Wait for readability rather than blocking in read(), so that _transferring can be
        // raised before the PTY is drained. The timeout only bounds how long shutdown waits.
        auto descriptor = pollfd { .fd = _ptyMaster, .events = POLLIN, .revents = 0 };
        int const ready = poll(&descriptor, 1, 50);

        if (ready == 0)
            continue; // Timed out; re-check _closed.
        if (ready < 0)
        {
            if (errno == EINTR)
                continue;
            break;
        }
        if ((descriptor.revents & POLLIN) == 0)
            break; // POLLHUP/POLLERR: the slave is gone, so no more output can arrive.

        // Raised before the read and cleared after the append, so the bytes are never
        // invisible: while this is set, output() keeps waiting even though the PTY is empty.
        _transferring.store(true, std::memory_order_release);

        char buffer[1024] {};
        ssize_t const readResult = read(_ptyMaster, buffer, sizeof(buffer));
        if (readResult > 0)
        {
            auto _ = std::scoped_lock { _outputMutex };
            _output.append(buffer, static_cast<size_t>(readResult));
        }
        _transferring.store(false, std::memory_order_release);

        if (readResult > 0)
            continue;
        if (readResult < 0 && (errno == EINTR || errno == EAGAIN))
            continue;

        // EOF, or EIO once the slave closed -- ordinary shutdown. Throwing here would cross
        // a thread boundary with nobody to catch it and abort the process, so just stop.
        break;
    }

    _transferring.store(false, std::memory_order_release);
    _readerRunning.store(false, std::memory_order_release);
}

#endif // !_WIN32

} // namespace endo
