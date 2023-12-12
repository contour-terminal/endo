// SPDX-License-Identifier: Apache-2.0
module;

#include <fmt/format.h>

#include <cstring>
#include <functional>

#include <sys/ioctl.h>
#include <thread>
#include <mutex>
#include <pty.h>
#include <termios.h>
#include <unistd.h>

import UnixPipe;
import IRGenerator;

export module TTY;
namespace endo
{

export class TTY
{
  public:
    TTY() = default;
    virtual ~TTY() = default;

    TTY(TTY const&) = delete;
    TTY& operator=(TTY const&) = delete;

    TTY(TTY&&) = default;
    TTY& operator=(TTY&&) = default;

    [[nodiscard]] virtual int inputFd() const noexcept = 0;
    [[nodiscard]] virtual int outputFd() const noexcept = 0;

    virtual void setRawMode() = 0;
    virtual void restoreMode() = 0;

    virtual void writeToStdout(std::string_view str) const = 0;

    template <typename... Args>
    void writeToStdout(fmt::format_string<Args...> const& fmt, Args&&... args) const
    {
        writeToStdout(fmt::format(fmt, std::forward<Args>(args)...));
    }

    virtual void writeToStdin(std::string_view str) const = 0;

    template <typename... Args>
    void writeToStdin(fmt::format_string<Args...> const& fmt, Args&&... args) const
    {
        writeToStdin(fmt::format(fmt, std::forward<Args>(args)...));
    }
};

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

export class RealTTY final: public TTY
{
  public:
    RealTTY()
    {
        for (int fd: { STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO })
            if (isatty(fd) && tcgetattr(fd, &_originalTermios) == 0)
                return;
        throw std::runtime_error("tcgetattr: " + std::string(strerror(errno)));
    }
    ~RealTTY() override { restoreMode(); }

    [[nodiscard]] static RealTTY& instance()
    {
        static RealTTY instance;
        return instance;
    }
    [[nodiscard]] int inputFd() const noexcept override { return STDIN_FILENO; }

    [[nodiscard]] int outputFd() const noexcept override { return STDOUT_FILENO; }

    void setRawMode() override { endo::setRawMode(STDIN_FILENO); }
    void restoreMode() override
    {
        int const result = tcsetattr(STDIN_FILENO, TCSAFLUSH, &_originalTermios);
        if (result == -1)
            throw std::runtime_error("tcsetattr: " + std::string(strerror(errno)));
    }

    void writeToStdout(std::string_view str) const override
    {
        ssize_t const result = ::write(STDOUT_FILENO, str.data(), str.size());
        if (result == -1)
            throw std::runtime_error("write: " + std::string(strerror(errno)));
    }
    void writeToStdin(std::string_view str) const override
    {
        ssize_t const result = ::write(STDIN_FILENO, str.data(), str.size());
        if (result == -1)
            throw std::runtime_error("write: " + std::string(strerror(errno)));
    }

  private:
    termios _originalTermios {};
};

// This is a TTY implementation that can be used for testing.
// It uses a PTY to simulate a TTY.
// The output of the TTY is stored in a buffer that can be inspected.
export class TestPTY final: public TTY
{
  public:
    TestPTY(): _windowSize { .ws_row = 25, .ws_col = 80, .ws_xpixel = 0, .ws_ypixel = 0 }
    {
        char name[256];
        if (openpty(&_ptyMaster, &_ptySlave, name, &_baseTermios, &_windowSize) == -1)
            throw std::runtime_error("openpty: " + std::string(strerror(errno)));

        fmt::print("TestPTY opened: {} (master {}, slave {})\n", name, _ptyMaster, _ptySlave);

        _updateThread = std::thread { std::bind(&TestPTY::outputUpdateLoop, this) };
    }
    ~TestPTY() override
    {
        _closed = true;
        pthread_cancel(_updateThread.native_handle());
        _updateThread.join();
        saveClose(&_ptySlave);
        saveClose(&_ptyMaster);
    }

    [[nodiscard]] int inputFd() const noexcept override { return _ptySlave; }
    [[nodiscard]] int outputFd() const noexcept override { return _ptySlave; }

    void setRawMode() override { endo::setRawMode(STDIN_FILENO); }
    void restoreMode() override
    {
        int const result = tcsetattr(STDIN_FILENO, TCSAFLUSH, &_baseTermios);
        if (result == -1)
            throw std::runtime_error("tcsetattr: " + std::string(strerror(errno)));
    }
    void writeToStdout(std::string_view str) const override
    {
        ssize_t const result = ::write(_ptySlave, str.data(), str.size());
        if (result == -1)
            throw std::runtime_error("write: " + std::string(strerror(errno)));
    }
    void writeToStdin(std::string_view str) const override
    {
        ssize_t const result = ::write(_ptyMaster, str.data(), str.size());
        if (result == -1)
            throw std::runtime_error("write: " + std::string(strerror(errno)));
    }

    // Returns the output that was written to the TTY.
    [[nodiscard]] std::string_view output() const noexcept
    {
        auto _ = std::scoped_lock { _outputMutex };
        return _output;
    }

  private:
    // This is the thread that reads from the output pipe and stores the output for later introspection.
    void outputUpdateLoop()
    {
        while (!_closed)
        {
            char buffer[1024];
            ssize_t const writeResult = read(_ptyMaster, buffer, sizeof(buffer));
            if (writeResult == 0)
                break;
            else if (writeResult > 0)
            {
                auto _ = std::lock_guard<std::mutex> { _outputMutex };
                _output.append(buffer, writeResult);
            }
            else if (errno == EINTR || errno == EAGAIN)
                continue;
            else
                throw std::runtime_error("read: " + std::string(strerror(errno)));
        }
    }

  private:
    std::string _output;
    std::thread _updateThread;
    mutable std::mutex _outputMutex;

    int _ptyMaster = -1;
    int _ptySlave = -1;
    termios _baseTermios {};
    winsize _windowSize {};

    bool _closed = false;
};
} // namespace endo
