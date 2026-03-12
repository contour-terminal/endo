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

    #include <poll.h>
    #include <pty.h>
    #include <unistd.h>
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

    _updateThread = std::thread { std::bind(&TestPTY::outputUpdateLoop, this) };
}

TestPTY::~TestPTY()
{
    _closed = true;
    pthread_cancel(_updateThread.native_handle());
    _updateThread.join();
    safeClose(&_ptySlave);
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

std::string_view TestPTY::output() const noexcept
{
    // Give the output thread time to read remaining data from the PTY
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto _ = std::scoped_lock { _outputMutex };
    return _output;
}

void TestPTY::outputUpdateLoop()
{
    while (!_closed)
    {
        char buffer[1024] {};
        ssize_t const writeResult = read(_ptyMaster, buffer, sizeof(buffer));
        if (writeResult == 0)
            break;
        else if (writeResult > 0)
        {
            auto _ = std::scoped_lock { _outputMutex };
            _output.append(buffer, writeResult);
        }
        else if (errno == EINTR || errno == EAGAIN)
            continue;
        else
            throw std::runtime_error("read: " + std::string(strerror(errno)));
    }
}

#endif // !_WIN32

} // namespace endo
