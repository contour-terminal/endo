#include <shell/TTY.h>

#include <cstring>
#include <functional>

#include <sys/ioctl.h>

#include <pty.h>
#include <termios.h>
#include <unistd.h>

import UnixPipe;

namespace endo
{

namespace
{
    void setRawMode(int fd)
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
} // namespace

//  {{{ RealTTY
RealTTY& RealTTY::instance()
{
    static RealTTY instance;
    return instance;
}

RealTTY::RealTTY()
{
    for (int fd: {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO})
        if (isatty(fd) && tcgetattr(fd, &_originalTermios) == 0)
            return;
    throw std::runtime_error("tcgetattr: " + std::string(strerror(errno)));
}

RealTTY::~RealTTY()
{
    restoreMode();
}

int RealTTY::inputFd() const noexcept
{
    return STDIN_FILENO;
}

int RealTTY::outputFd() const noexcept
{
    return STDOUT_FILENO;
}

void RealTTY::setRawMode()
{
    endo::setRawMode(STDIN_FILENO);
}

void RealTTY::restoreMode()
{
    int const result = tcsetattr(STDIN_FILENO, TCSAFLUSH, &_originalTermios);
    if (result == -1)
        throw std::runtime_error("tcsetattr: " + std::string(strerror(errno)));
}

void RealTTY::writeToStdin(std::string_view str) const
{
    ssize_t const result = ::write(STDIN_FILENO, str.data(), str.size());
    if (result == -1)
        throw std::runtime_error("write: " + std::string(strerror(errno)));
}

void RealTTY::writeToStdout(std::string_view str) const
{
    ssize_t const result = ::write(STDOUT_FILENO, str.data(), str.size());
    if (result == -1)
        throw std::runtime_error("write: " + std::string(strerror(errno)));
}
// }}}

// {{{ TestPTY
TestPTY::TestPTY(): _windowSize { .ws_row = 25, .ws_col = 80, .ws_xpixel = 0, .ws_ypixel = 0 }
{
    char name[256];
    if (openpty(&_ptyMaster, &_ptySlave, name, &_baseTermios, &_windowSize) == -1)
        throw std::runtime_error("openpty: " + std::string(strerror(errno)));

    fmt::print("TestPTY opened: {} (master {}, slave {})\n", name, _ptyMaster, _ptySlave);

    _updateThread = std::thread { std::bind(&TestPTY::outputUpdateLoop, this) };
}

TestPTY::~TestPTY()
{
    _closed = true;
    pthread_cancel(_updateThread.native_handle());
    _updateThread.join();
    saveClose(&_ptySlave);
    saveClose(&_ptyMaster);
}

void TestPTY::outputUpdateLoop()
{
    while (!_closed)
    {
        char buffer[1024];
        ssize_t const writeResult = read(_ptyMaster, buffer, sizeof(buffer));
        if (writeResult == 0)
            break;
        else if (writeResult > 0)
        {
            auto _ = std::lock_guard { _outputMutex };
            _output.append(buffer, writeResult);
        }
        else if (errno == EINTR || errno == EAGAIN)
            continue;
        else
            throw std::runtime_error("read: " + std::string(strerror(errno)));
    }
}

int TestPTY::inputFd() const noexcept
{
    return _ptySlave;
}

int TestPTY::outputFd() const noexcept
{
    return _ptySlave;
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

void TestPTY::writeToStdin(std::string_view str) const
{
    ssize_t const result = ::write(_ptyMaster, str.data(), str.size());
    if (result == -1)
        throw std::runtime_error("write: " + std::string(strerror(errno)));
}

void TestPTY::writeToStdout(std::string_view str) const
{
    ssize_t const result = ::write(_ptySlave, str.data(), str.size());
    if (result == -1)
        throw std::runtime_error("write: " + std::string(strerror(errno)));
}

std::string_view TestPTY::output() const noexcept
{
    auto _ = std::scoped_lock { _outputMutex };
    return _output;
}
// }}}

} // namespace endo
