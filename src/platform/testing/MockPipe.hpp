// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

#include <platform/Pipe.hpp>

namespace endo::platform::testing
{

/// @brief Creates a NativeHandle from an integer value (for test purposes only).
/// @param value The integer value to convert.
/// @return A NativeHandle representing the value.
inline NativeHandle testHandle(uintptr_t value)
{
#ifdef _WIN32
    return reinterpret_cast<HANDLE>(value);
#else
    return static_cast<int>(value);
#endif
}

/// Mock Pipe for unit testing.
///
/// Provides configurable reader/writer handles without creating real OS pipes.
class MockPipe final: public Pipe
{
  public:
    /// Creates a mock pipe with the given handle values.
    explicit MockPipe(NativeHandle reader = testHandle(10), NativeHandle writer = testHandle(11)):
        _reader(reader), _writer(writer)
    {
    }

    [[nodiscard]] NativeHandle reader() const noexcept override { return _reader; }

    [[nodiscard]] NativeHandle writer() const noexcept override { return _writer; }

    [[nodiscard]] NativeHandle releaseReader() noexcept override
    {
        auto const fd = _reader;
        _reader = InvalidHandle;
        return fd;
    }

    [[nodiscard]] NativeHandle releaseWriter() noexcept override
    {
        auto const fd = _writer;
        _writer = InvalidHandle;
        return fd;
    }

    void closeReader() noexcept override { _reader = InvalidHandle; }

    void closeWriter() noexcept override { _writer = InvalidHandle; }

    [[nodiscard]] bool good() const noexcept override
    {
        return _reader != InvalidHandle && _writer != InvalidHandle;
    }

  private:
    NativeHandle _reader;
    NativeHandle _writer;
};

} // namespace endo::platform::testing
