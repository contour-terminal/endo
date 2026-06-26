// SPDX-License-Identifier: Apache-2.0
#include "Pipe.hpp"

#include <crispy/logstore.h>

#include <cstring>
#include <stdexcept>
#include <string>

#if !defined(_WIN32)
    #include <fcntl.h>
    #include <unistd.h>
#endif

namespace endo::platform
{

#if !defined(_WIN32)

namespace
{
    /// Lazily-initialized log category for platform pipe operations.
    ///
    /// A function-local static (initialized on first use) is used instead of a
    /// namespace-scope static so the potentially-throwing @c logstore::category
    /// construction does not run during static initialization
    /// (bugprone-throwing-static-initialization).
    auto& pipeLog()
    {
        static auto pipeTag = logstore::category("platform.pipe", "Platform pipe operations");
        return pipeTag;
    }
} // namespace

using namespace std::string_literals;

/// Safely closes a file descriptor and sets it to InvalidHandle.
///
/// @param fd Pointer to the file descriptor to close
void safeClosePipe(NativeHandle* fd) noexcept
{
    if (fd && *fd != InvalidHandle)
    {
        pipeLog()()("Closing fd {}\n", *fd);
        ::close(*fd); // NOLINT(clang-analyzer-unix.StdCLibraryFunctions)
        *fd = InvalidHandle;
    }
}

/// POSIX implementation of the Pipe interface.
class PosixPipe final: public Pipe
{
  public:
    /// Creates a POSIX pipe with the specified flags.
    ///
    /// @param flags Pipe creation flags (e.g., O_CLOEXEC, O_NONBLOCK)
    /// @throws std::runtime_error if pipe creation fails
    explicit PosixPipe(unsigned flags = 0): _pfd { InvalidHandle, InvalidHandle }
    {
    #if defined(__linux__)
        if (pipe2(_pfd, static_cast<int>(flags)) < 0)
            throw std::runtime_error { "Failed to create pipe. "s + strerror(errno) };
    #else
        if (pipe(_pfd) < 0)
            throw std::runtime_error { "Failed to create pipe. "s + strerror(errno) };
        // Apply flags manually on non-Linux systems
        if (flags != 0)
        {
            for (auto const fd: _pfd)
            {
                int const currentFlags = fcntl(fd, F_GETFD);
                if (currentFlags != -1)
                    fcntl(fd, F_SETFD, currentFlags | static_cast<int>(flags));
            }
        }
    #endif
        pipeLog()()("Created pipe: {} {}\n", _pfd[0], _pfd[1]);
    }

    ~PosixPipe() override { close(); }

    PosixPipe(PosixPipe&& other) noexcept: _pfd { other._pfd[0], other._pfd[1] }
    {
        other._pfd[0] = InvalidHandle;
        other._pfd[1] = InvalidHandle;
    }

    PosixPipe& operator=(PosixPipe&& other) noexcept
    {
        if (this != &other)
        {
            close();
            _pfd[0] = other._pfd[0];
            _pfd[1] = other._pfd[1];
            other._pfd[0] = InvalidHandle;
            other._pfd[1] = InvalidHandle;
        }
        return *this;
    }

    [[nodiscard]] NativeHandle reader() const noexcept override { return _pfd[0]; }

    [[nodiscard]] NativeHandle writer() const noexcept override { return _pfd[1]; }

    [[nodiscard]] NativeHandle releaseReader() noexcept override
    {
        auto const fd = _pfd[0];
        _pfd[0] = InvalidHandle;
        return fd;
    }

    [[nodiscard]] NativeHandle releaseWriter() noexcept override
    {
        auto const fd = _pfd[1];
        _pfd[1] = InvalidHandle;
        return fd;
    }

    void closeReader() noexcept override { safeClosePipe(&_pfd[0]); }

    void closeWriter() noexcept override { safeClosePipe(&_pfd[1]); }

    [[nodiscard]] bool good() const noexcept override
    {
        return _pfd[0] != InvalidHandle && _pfd[1] != InvalidHandle;
    }

  private:
    void close()
    {
        closeReader();
        closeWriter();
    }

    NativeHandle _pfd[2] { InvalidHandle, InvalidHandle };
};

std::expected<std::unique_ptr<Pipe>, PlatformError> createPipe(unsigned flags)
{
    try
    {
        return std::make_unique<PosixPipe>(flags);
    }
    catch (std::runtime_error const&)
    {
        return std::unexpected(PlatformError::PipeCreationFailed);
    }
}

#endif // !_WIN32

} // namespace endo::platform
