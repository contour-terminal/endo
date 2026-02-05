// SPDX-License-Identifier: Apache-2.0
module;

#include <chrono>
#include <cstring>
#include <expected>
#include <format>
#include <functional>
#include <mutex>
#include <print>
#include <thread>

#include "Error.h"
#include "Platform.h"

#if !defined(_WIN32)
    #include <sys/ioctl.h>

    #include <pty.h>
    #include <termios.h>
    #include <unistd.h>
#endif

import Pipe;
import IRGenerator;

export module TTY;

namespace endo
{

/// Terminal size in rows and columns.
export struct TerminalSize
{
    uint16_t rows = 0; ///< Number of rows
    uint16_t cols = 0; ///< Number of columns
};

/// Abstract interface for terminal operations.
///
/// This interface abstracts platform-specific terminal operations, enabling
/// testability and cross-platform support.
export class TTY
{
  public:
    TTY() = default;
    virtual ~TTY() = default;

    TTY(TTY const&) = delete;
    TTY& operator=(TTY const&) = delete;

    TTY(TTY&&) = default;
    TTY& operator=(TTY&&) = default;

    /// Returns the native handle for input.
    [[nodiscard]] virtual NativeHandle inputFd() const noexcept = 0;

    /// Returns the native handle for output.
    [[nodiscard]] virtual NativeHandle outputFd() const noexcept = 0;

    /// Checks if this TTY is connected to a real terminal.
    [[nodiscard]] virtual bool isTerminal() const noexcept = 0;

    /// Gets the terminal size.
    ///
    /// @return Terminal size on success, or an error
    [[nodiscard]] virtual std::expected<TerminalSize, ShellError> getSize() const = 0;

    virtual void setRawMode() = 0;
    virtual void restoreMode() = 0;

    virtual void writeToStdout(std::string_view str) const = 0;

    template <typename... Args>
    void writeToStdout(std::format_string<Args...> const& fmt, Args&&... args) const
    {
        writeToStdout(std::format(fmt, std::forward<Args>(args)...));
    }

    virtual void writeToStdin(std::string_view str) const = 0;

    template <typename... Args>
    void writeToStdin(std::format_string<Args...> const& fmt, Args&&... args) const
    {
        writeToStdin(std::format(fmt, std::forward<Args>(args)...));
    }
};

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

/// Safely closes a file descriptor and sets it to InvalidHandle.
///
/// @param fd Pointer to the file descriptor to close
void safeClose(NativeHandle* fd) noexcept
{
    if (fd && *fd != InvalidHandle)
    {
        ::close(*fd);
        *fd = InvalidHandle;
    }
}

export class RealTTY final: public TTY
{
  public:
    RealTTY()
    {
        for (NativeHandle fd: { STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO })
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

    [[nodiscard]] NativeHandle inputFd() const noexcept override { return STDIN_FILENO; }

    [[nodiscard]] NativeHandle outputFd() const noexcept override { return STDOUT_FILENO; }

    [[nodiscard]] bool isTerminal() const noexcept override { return isatty(STDIN_FILENO) != 0; }

    [[nodiscard]] std::expected<TerminalSize, ShellError> getSize() const override
    {
        winsize ws {};
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1)
            return std::unexpected(ShellError::IoError);
        return TerminalSize { .rows = ws.ws_row, .cols = ws.ws_col };
    }

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
#endif // !_WIN32

#if !defined(_WIN32)
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

        std::println("TestPTY opened: {} (master {}, slave {})", name, _ptyMaster, _ptySlave);

        _updateThread = std::thread { std::bind(&TestPTY::outputUpdateLoop, this) };
    }

    ~TestPTY() override
    {
        _closed = true;
        pthread_cancel(_updateThread.native_handle());
        _updateThread.join();
        safeClose(&_ptySlave);
        safeClose(&_ptyMaster);
    }

    [[nodiscard]] NativeHandle inputFd() const noexcept override { return _ptySlave; }

    [[nodiscard]] NativeHandle outputFd() const noexcept override { return _ptySlave; }

    [[nodiscard]] bool isTerminal() const noexcept override { return isatty(_ptySlave) != 0; }

    [[nodiscard]] std::expected<TerminalSize, ShellError> getSize() const override
    {
        return TerminalSize { .rows = _windowSize.ws_row, .cols = _windowSize.ws_col };
    }

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
        // Give the output thread time to read remaining data from the PTY
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
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

    NativeHandle _ptyMaster = InvalidHandle;
    NativeHandle _ptySlave = InvalidHandle;
    termios _baseTermios {};
    winsize _windowSize {};

    bool _closed = false;
};
#endif // !_WIN32

} // namespace endo
