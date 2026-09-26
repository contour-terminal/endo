// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

#include <platform/Pipe.hpp>

namespace endo::platform::testing
{

/// @brief Creates a NativeHandle from an integer value (for test purposes only).
/// @param value The integer value to convert.
/// @return A NativeHandle representing the value.
inline core::platform::NativeHandle testHandle(uintptr_t value)
{
#ifdef _WIN32
    return reinterpret_cast<core::platform::NativeHandle>(value);
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
    explicit MockPipe(core::platform::NativeHandle reader = testHandle(10),
                      core::platform::NativeHandle writer = testHandle(11)):
        _reader(reader), _writer(writer)
    {
    }

    [[nodiscard]] core::platform::NativeHandle reader() const noexcept override { return _reader; }

    [[nodiscard]] core::platform::NativeHandle writer() const noexcept override { return _writer; }

    [[nodiscard]] core::platform::NativeHandle releaseReader() noexcept override
    {
        auto const fd = _reader;
        _reader = core::platform::InvalidHandle;
        return fd;
    }

    [[nodiscard]] core::platform::NativeHandle releaseWriter() noexcept override
    {
        auto const fd = _writer;
        _writer = core::platform::InvalidHandle;
        return fd;
    }

    void closeReader() noexcept override { _reader = core::platform::InvalidHandle; }

    void closeWriter() noexcept override { _writer = core::platform::InvalidHandle; }

    [[nodiscard]] bool good() const noexcept override
    {
        return _reader != core::platform::InvalidHandle && _writer != core::platform::InvalidHandle;
    }

  private:
    core::platform::NativeHandle _reader;
    core::platform::NativeHandle _writer;
};

} // namespace endo::platform::testing
