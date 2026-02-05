// SPDX-License-Identifier: Apache-2.0
#include "TTY.hpp"

#include <cstring>
#include <expected>
#include <format>
#include <string>
#include <string_view>

#include "Error.hpp"
#include "Platform.hpp"

#if defined(_WIN32)
    #include <windows.h>
#endif

namespace endo
{

#if defined(_WIN32)

/// Windows implementation of the TTY interface using ConPTY.
///
/// This is a stub implementation that will be completed when Windows support is needed.
class WindowsTTY final: public TTY
{
  public:
    WindowsTTY()
    {
        _hStdin = GetStdHandle(STD_INPUT_HANDLE);
        _hStdout = GetStdHandle(STD_OUTPUT_HANDLE);

        if (_hStdin == INVALID_HANDLE_VALUE || _hStdout == INVALID_HANDLE_VALUE)
            throw std::runtime_error("Failed to get console handles");

        // Save original console mode
        if (!GetConsoleMode(_hStdin, &_originalInputMode))
            throw std::runtime_error("Failed to get console mode");
    }

    ~WindowsTTY() override { restoreMode(); }

    [[nodiscard]] static WindowsTTY& instance()
    {
        static WindowsTTY instance;
        return instance;
    }

    [[nodiscard]] NativeHandle inputFd() const noexcept override { return _hStdin; }

    [[nodiscard]] NativeHandle outputFd() const noexcept override { return _hStdout; }

    [[nodiscard]] bool isTerminal() const noexcept override
    {
        DWORD mode;
        return GetConsoleMode(_hStdin, &mode) != 0;
    }

    [[nodiscard]] std::expected<TerminalSize, ShellError> getSize() const override
    {
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if (!GetConsoleScreenBufferInfo(_hStdout, &csbi))
            return std::unexpected(ShellError::IoError);

        return TerminalSize { .rows = static_cast<uint16_t>(csbi.srWindow.Bottom - csbi.srWindow.Top + 1),
                              .cols = static_cast<uint16_t>(csbi.srWindow.Right - csbi.srWindow.Left + 1) };
    }

    void setRawMode() override
    {
        // Enable virtual terminal processing and disable line input
        DWORD mode = ENABLE_VIRTUAL_TERMINAL_INPUT;
        SetConsoleMode(_hStdin, mode);

        // Enable virtual terminal processing for output
        DWORD outMode = ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(_hStdout, outMode);
    }

    void restoreMode() override { SetConsoleMode(_hStdin, _originalInputMode); }

    void writeToStdout(std::string_view str) const override
    {
        DWORD written;
        WriteConsoleA(_hStdout, str.data(), static_cast<DWORD>(str.size()), &written, nullptr);
    }

    void writeToStdin(std::string_view str) const override
    {
        // Writing to stdin is not directly supported on Windows
        // This would typically involve input record injection
        (void) str;
    }

  private:
    NativeHandle _hStdin = InvalidHandle;
    NativeHandle _hStdout = InvalidHandle;
    DWORD _originalInputMode = 0;
};

#endif // _WIN32

} // namespace endo
