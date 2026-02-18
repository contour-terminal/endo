// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <expected>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include "Error.hpp"
#include <platform/Types.hpp>

#if !defined(_WIN32)
    #include <sys/ioctl.h>

    #include <termios.h>
#endif

namespace endo
{

/// Terminal size in rows and columns.
struct TerminalSize
{
    uint16_t rows = 0; ///< Number of rows
    uint16_t cols = 0; ///< Number of columns
};

/// Abstract interface for terminal operations.
///
/// This interface abstracts platform-specific terminal operations, enabling
/// testability and cross-platform support.
class TTY
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

    /// Enable or disable input echo (for silent mode in read -s)
    /// @param enabled true to enable echo, false to disable
    virtual void setEchoEnabled(bool enabled) = 0;

    /// Read a single character with optional timeout
    /// @param timeout Maximum time to wait (0 = block indefinitely)
    /// @return Character read, or std::nullopt on timeout/EOF/error
    [[nodiscard]] virtual std::optional<char> readCharWithTimeout(
        std::chrono::milliseconds timeout = std::chrono::milliseconds::zero()) = 0;

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

#if defined(_WIN32)

/// Windows implementation of the TTY interface.
///
/// Uses Console API for terminal operations, with VT processing enabled for
/// escape sequence support. Defined in platform/WindowsTTY.cpp.
class WindowsTTY final: public TTY
{
  public:
    WindowsTTY();
    ~WindowsTTY() override;

    [[nodiscard]] static WindowsTTY& instance();

    [[nodiscard]] NativeHandle inputFd() const noexcept override;
    [[nodiscard]] NativeHandle outputFd() const noexcept override;
    [[nodiscard]] bool isTerminal() const noexcept override;
    [[nodiscard]] std::expected<TerminalSize, ShellError> getSize() const override;
    void setRawMode() override;
    void restoreMode() override;
    void setEchoEnabled(bool enabled) override;
    [[nodiscard]] std::optional<char> readCharWithTimeout(
        std::chrono::milliseconds timeout = std::chrono::milliseconds::zero()) override;
    void writeToStdout(std::string_view str) const override;
    void writeToStdin(std::string_view str) const override;

  private:
    NativeHandle _hStdin = InvalidHandle;
    NativeHandle _hStdout = InvalidHandle;
    unsigned long _originalInputMode = 0;
    unsigned long _originalOutputMode = 0;
};

/// Windows test TTY implementation using pipes.
///
/// Unlike POSIX PTY which provides true terminal semantics, this implementation
/// uses anonymous pipes for I/O redirection. Tests will see isTerminal() = false,
/// which is acceptable for test isolation. Defined in platform/WindowsTestPTY.cpp.
class WindowsTestPTY final: public TTY
{
  public:
    WindowsTestPTY();
    ~WindowsTestPTY() override;

    [[nodiscard]] NativeHandle inputFd() const noexcept override;
    [[nodiscard]] NativeHandle outputFd() const noexcept override;
    [[nodiscard]] bool isTerminal() const noexcept override;
    [[nodiscard]] std::expected<TerminalSize, ShellError> getSize() const override;
    void setRawMode() override;
    void restoreMode() override;
    void setEchoEnabled(bool enabled) override;
    [[nodiscard]] std::optional<char> readCharWithTimeout(
        std::chrono::milliseconds timeout = std::chrono::milliseconds::zero()) override;
    void writeToStdout(std::string_view str) const override;
    void writeToStdin(std::string_view str) const override;

    /// Returns the output that was written to the TTY.
    [[nodiscard]] std::string_view output() const noexcept;

  private:
    void outputCaptureLoop();

    // Input pipe: writeInputHandle -> readInputHandle (simulates stdin)
    NativeHandle _readInputHandle = InvalidHandle;
    NativeHandle _writeInputHandle = InvalidHandle;

    // Output pipe: writeOutputHandle -> readOutputHandle (captures stdout)
    NativeHandle _readOutputHandle = InvalidHandle;
    NativeHandle _writeOutputHandle = InvalidHandle;

    // Output capture
    std::string _output;
    std::thread _captureThread;
    mutable std::mutex _outputMutex;
    bool _closed = false;

    // Terminal size (fixed for tests)
    TerminalSize _terminalSize { .rows = 25, .cols = 80 };
};

using TestPTY = WindowsTestPTY;

#else  // POSIX

void setRawMode(NativeHandle fd);

/// Safely closes a file descriptor and sets it to InvalidHandle.
///
/// @param fd Pointer to the file descriptor to close
void safeClose(NativeHandle* fd) noexcept;

class RealTTY final: public TTY
{
  public:
    RealTTY();
    ~RealTTY() override;

    [[nodiscard]] static RealTTY& instance();

    [[nodiscard]] NativeHandle inputFd() const noexcept override;
    [[nodiscard]] NativeHandle outputFd() const noexcept override;
    [[nodiscard]] bool isTerminal() const noexcept override;
    [[nodiscard]] std::expected<TerminalSize, ShellError> getSize() const override;
    void setRawMode() override;
    void restoreMode() override;
    void setEchoEnabled(bool enabled) override;
    [[nodiscard]] std::optional<char> readCharWithTimeout(
        std::chrono::milliseconds timeout = std::chrono::milliseconds::zero()) override;
    void writeToStdout(std::string_view str) const override;
    void writeToStdin(std::string_view str) const override;

  private:
    termios _originalTermios {};
    bool _hasTTY = false; ///< Whether a real TTY is available
};

// This is a TTY implementation that can be used for testing.
// It uses a PTY to simulate a TTY.
// The output of the TTY is stored in a buffer that can be inspected.
class TestPTY final: public TTY
{
  public:
    TestPTY();
    ~TestPTY() override;

    [[nodiscard]] NativeHandle inputFd() const noexcept override;
    [[nodiscard]] NativeHandle outputFd() const noexcept override;
    [[nodiscard]] bool isTerminal() const noexcept override;
    [[nodiscard]] std::expected<TerminalSize, ShellError> getSize() const override;
    void setRawMode() override;
    void restoreMode() override;
    void setEchoEnabled(bool enabled) override;
    [[nodiscard]] std::optional<char> readCharWithTimeout(
        std::chrono::milliseconds timeout = std::chrono::milliseconds::zero()) override;
    void writeToStdout(std::string_view str) const override;
    void writeToStdin(std::string_view str) const override;

    // Returns the output that was written to the TTY.
    [[nodiscard]] std::string_view output() const noexcept;

  private:
    void outputUpdateLoop();

    std::string _output;
    std::thread _updateThread;
    mutable std::mutex _outputMutex;

    NativeHandle _ptyMaster = InvalidHandle;
    NativeHandle _ptySlave = InvalidHandle;
    termios _baseTermios {};
    struct winsize _windowSize;

    bool _closed = false;
};
#endif // !_WIN32

} // namespace endo
