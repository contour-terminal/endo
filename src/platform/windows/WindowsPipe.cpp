// SPDX-License-Identifier: Apache-2.0
#include <platform/Pipe.hpp>

#if defined(_WIN32)
    #include <stdexcept>

    #include <windows.h>

namespace endo::platform
{

/// Windows implementation of the Pipe interface.
class WindowsPipe final: public Pipe
{
  public:
    /// Creates a Windows pipe.
    ///
    /// @param flags Creation flags (currently unused on Windows)
    explicit WindowsPipe([[maybe_unused]] unsigned flags = 0):
        _readHandle(InvalidHandle), _writeHandle(InvalidHandle)
    {
        SECURITY_ATTRIBUTES sa;
        sa.nLength = sizeof(SECURITY_ATTRIBUTES);
        sa.bInheritHandle = TRUE;
        sa.lpSecurityDescriptor = nullptr;

        if (!CreatePipe(&_readHandle, &_writeHandle, &sa, 0))
            throw std::runtime_error("Failed to create pipe");
    }

    ~WindowsPipe() override { close(); }

    WindowsPipe(WindowsPipe&& other) noexcept:
        _readHandle(other._readHandle), _writeHandle(other._writeHandle)
    {
        other._readHandle = InvalidHandle;
        other._writeHandle = InvalidHandle;
    }

    WindowsPipe& operator=(WindowsPipe&& other) noexcept
    {
        if (this != &other)
        {
            close();
            _readHandle = other._readHandle;
            _writeHandle = other._writeHandle;
            other._readHandle = InvalidHandle;
            other._writeHandle = InvalidHandle;
        }
        return *this;
    }

    [[nodiscard]] NativeHandle reader() const noexcept override { return _readHandle; }

    [[nodiscard]] NativeHandle writer() const noexcept override { return _writeHandle; }

    [[nodiscard]] NativeHandle releaseReader() noexcept override
    {
        auto const handle = _readHandle;
        _readHandle = InvalidHandle;
        return handle;
    }

    [[nodiscard]] NativeHandle releaseWriter() noexcept override
    {
        auto const handle = _writeHandle;
        _writeHandle = InvalidHandle;
        return handle;
    }

    void closeReader() noexcept override
    {
        if (_readHandle != InvalidHandle)
        {
            CloseHandle(_readHandle);
            _readHandle = InvalidHandle;
        }
    }

    void closeWriter() noexcept override
    {
        if (_writeHandle != InvalidHandle)
        {
            CloseHandle(_writeHandle);
            _writeHandle = InvalidHandle;
        }
    }

    [[nodiscard]] bool good() const noexcept override
    {
        return _readHandle != InvalidHandle && _writeHandle != InvalidHandle;
    }

  private:
    void close()
    {
        closeReader();
        closeWriter();
    }

    NativeHandle _readHandle;
    NativeHandle _writeHandle;
};

std::expected<std::unique_ptr<Pipe>, PlatformError> createPipe(unsigned flags)
{
    try
    {
        return std::make_unique<WindowsPipe>(flags);
    }
    catch (std::runtime_error const&)
    {
        return std::unexpected(PlatformError::PipeCreationFailed);
    }
}

} // namespace endo::platform

#endif // _WIN32
