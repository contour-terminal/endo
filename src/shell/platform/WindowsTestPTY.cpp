// SPDX-License-Identifier: Apache-2.0
#include <chrono>
#include <format>
#include <mutex>
#include <stdexcept>
#include <thread>

#include <Windows.h>

#include "../TTY.hpp"

namespace endo
{

#if defined(_WIN32)

// WindowsTestPTY implementation
// Class is declared in TTY.hpp

WindowsTestPTY::WindowsTestPTY()
{
    SECURITY_ATTRIBUTES sa {};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    // Create input pipe (for simulating stdin)
    if (!CreatePipe(&_readInputHandle, &_writeInputHandle, &sa, 0))
    {
        throw std::runtime_error(
            std::format("CreatePipe (input) failed: {}", static_cast<int>(GetLastError())));
    }

    // Create output pipe (for capturing stdout)
    if (!CreatePipe(&_readOutputHandle, &_writeOutputHandle, &sa, 0))
    {
        CloseHandle(_readInputHandle);
        CloseHandle(_writeInputHandle);
        throw std::runtime_error(
            std::format("CreatePipe (output) failed: {}", static_cast<int>(GetLastError())));
    }

    // Make sure the write end of the input pipe is not inherited
    if (!SetHandleInformation(_writeInputHandle, HANDLE_FLAG_INHERIT, 0))
    {
        CloseHandle(_readInputHandle);
        CloseHandle(_writeInputHandle);
        CloseHandle(_readOutputHandle);
        CloseHandle(_writeOutputHandle);
        throw std::runtime_error(
            std::format("SetHandleInformation failed: {}", static_cast<int>(GetLastError())));
    }

    // Make sure the read end of the output pipe is not inherited
    if (!SetHandleInformation(_readOutputHandle, HANDLE_FLAG_INHERIT, 0))
    {
        CloseHandle(_readInputHandle);
        CloseHandle(_writeInputHandle);
        CloseHandle(_readOutputHandle);
        CloseHandle(_writeOutputHandle);
        throw std::runtime_error(
            std::format("SetHandleInformation failed: {}", static_cast<int>(GetLastError())));
    }

    // Start background thread to capture output
    _captureThread = std::thread { [this]() { outputCaptureLoop(); } };
}

WindowsTestPTY::~WindowsTestPTY()
{
    _closed = true;

    // Close write end of output pipe to signal EOF to capture thread
    if (_writeOutputHandle != InvalidHandle)
    {
        CloseHandle(_writeOutputHandle);
        _writeOutputHandle = InvalidHandle;
    }

    // Wait for capture thread to finish
    if (_captureThread.joinable())
        _captureThread.join();

    // Close remaining handles
    if (_readInputHandle != InvalidHandle)
        CloseHandle(_readInputHandle);
    if (_writeInputHandle != InvalidHandle)
        CloseHandle(_writeInputHandle);
    if (_readOutputHandle != InvalidHandle)
        CloseHandle(_readOutputHandle);
}

NativeHandle WindowsTestPTY::inputFd() const noexcept
{
    return _readInputHandle;
}

NativeHandle WindowsTestPTY::outputFd() const noexcept
{
    return _writeOutputHandle;
}

bool WindowsTestPTY::isTerminal() const noexcept
{
    // Pipes are not terminals
    return false;
}

std::expected<TerminalSize, ShellError> WindowsTestPTY::getSize() const
{
    return _terminalSize;
}

void WindowsTestPTY::setRawMode()
{
    // No-op: pipes don't have terminal modes
}

void WindowsTestPTY::restoreMode()
{
    // No-op: pipes don't have terminal modes
}

void WindowsTestPTY::setEchoEnabled([[maybe_unused]] bool enabled)
{
    // No-op: pipes don't have echo control
}

std::optional<char> WindowsTestPTY::readCharWithTimeout(std::chrono::milliseconds timeout)
{
    // For pipes, we need to poll for data availability
    if (timeout.count() > 0)
    {
        auto const start = std::chrono::steady_clock::now();
        while (true)
        {
            // Check if data is available
            DWORD bytesAvailable = 0;
            if (PeekNamedPipe(_readInputHandle, nullptr, 0, nullptr, &bytesAvailable, nullptr)
                && bytesAvailable > 0)
            {
                break; // Data available
            }

            // Check timeout
            auto const elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed >= timeout)
                return std::nullopt; // Timeout

            // Sleep briefly to avoid busy-waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    // Read one character
    char ch;
    DWORD bytesRead = 0;
    if (!ReadFile(_readInputHandle, &ch, 1, &bytesRead, nullptr) || bytesRead == 0)
        return std::nullopt; // EOF or error

    return ch;
}

void WindowsTestPTY::writeToStdout(std::string_view str) const
{
    DWORD bytesWritten = 0;
    if (!WriteFile(_writeOutputHandle, str.data(), static_cast<DWORD>(str.size()), &bytesWritten, nullptr))
    {
        throw std::runtime_error(
            std::format("WriteFile (output) failed: {}", static_cast<int>(GetLastError())));
    }
}

void WindowsTestPTY::writeToStderr(std::string_view str) const
{
    // In test mode, stderr output goes to the same output pipe as stdout.
    DWORD bytesWritten = 0;
    if (!WriteFile(_writeOutputHandle, str.data(), static_cast<DWORD>(str.size()), &bytesWritten, nullptr))
    {
        throw std::runtime_error(
            std::format("WriteFile (stderr) failed: {}", static_cast<int>(GetLastError())));
    }
}

bool WindowsTestPTY::isStderrTerminal() const noexcept
{
    // Pipes are not terminals.
    return false;
}

void WindowsTestPTY::writeToStdin(std::string_view str) const
{
    DWORD bytesWritten = 0;
    if (!WriteFile(_writeInputHandle, str.data(), static_cast<DWORD>(str.size()), &bytesWritten, nullptr))
    {
        throw std::runtime_error(
            std::format("WriteFile (input) failed: {}", static_cast<int>(GetLastError())));
    }
}

void WindowsTestPTY::setSize(uint16_t rows, uint16_t cols)
{
    _terminalSize = TerminalSize { .rows = rows, .cols = cols };
}

std::string_view WindowsTestPTY::output() const noexcept
{
    // Wait until the capture thread has drained all data from the pipe.
    // After shell.execute() returns, all writes to the pipe are done.
    // PeekNamedPipe reports 0 bytes once the capture thread has read everything.
    auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline)
    {
        DWORD bytesAvailable = 0;
        if (!PeekNamedPipe(_readOutputHandle, nullptr, 0, nullptr, &bytesAvailable, nullptr))
            break; // Pipe error (broken/closed)
        if (bytesAvailable == 0)
            break; // All data consumed by capture thread
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // Small grace period for the capture thread to finish appending under mutex
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    auto _ = std::scoped_lock { _outputMutex };
    return _output;
}

void WindowsTestPTY::outputCaptureLoop()
{
    constexpr DWORD BufferSize = 1024;
    char buffer[BufferSize];

    while (!_closed)
    {
        DWORD bytesAvailable = 0;
        if (!PeekNamedPipe(_readOutputHandle, nullptr, 0, nullptr, &bytesAvailable, nullptr))
        {
            if (_closed.load() || GetLastError() == ERROR_BROKEN_PIPE)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue; // Retry on transient failures
        }

        if (bytesAvailable == 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        DWORD bytesRead = 0;
        BOOL const success =
            ReadFile(_readOutputHandle, buffer, std::min(BufferSize, bytesAvailable), &bytesRead, nullptr);

        if (!success || bytesRead == 0)
            break; // EOF or error

        auto _ = std::lock_guard<std::mutex> { _outputMutex };
        _output.append(buffer, bytesRead);
    }
}

#endif // _WIN32

} // namespace endo
