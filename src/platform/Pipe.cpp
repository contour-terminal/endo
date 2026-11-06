// SPDX-License-Identifier: Apache-2.0
#include "Pipe.hpp"

#include <core/log/LogStore.hpp>

#include <array>
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
    /// namespace-scope static so the potentially-throwing @c core::log::Category
    /// construction does not run during static initialization
    /// (bugprone-throwing-static-initialization).
    auto& pipeLog()
    {
        static auto pipeTag = core::log::Category("platform.pipe", "Platform pipe operations");
        return pipeTag;
    }
} // namespace

using namespace std::string_literals;

/// Safely closes a file descriptor and sets it to InvalidHandle.
///
/// @param fd Pointer to the file descriptor to close
void safeClosePipe(core::platform::NativeHandle* fd) noexcept
{
    if (fd && *fd != core::platform::InvalidHandle)
    {
        pipeLog()()("Closing fd {}\n", *fd);
        ::close(*fd); // NOLINT(clang-analyzer-unix.StdCLibraryFunctions)
        *fd = core::platform::InvalidHandle;
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
    explicit PosixPipe(unsigned flags = 0):
        _pfd { core::platform::InvalidHandle, core::platform::InvalidHandle }
    {
    #if defined(__linux__)
        if (pipe2(_pfd, static_cast<int>(flags)) < 0)
            throw std::runtime_error { "Failed to create pipe. "s + strerror(errno) };
    #else
        if (pipe(_pfd) < 0)
            throw std::runtime_error { "Failed to create pipe. "s + strerror(errno) };

        // pipe2's flags, applied one by one where there is no pipe2. Each is a different kind of
        // flag: O_CLOEXEC is the descriptor flag FD_CLOEXEC (F_SETFD), O_NONBLOCK a file status flag
        // (F_SETFL). Handing the open flag itself to F_SETFD sets nothing at all.
        struct FlagMapping
        {
            unsigned requested; ///< The pipe2 flag.
            int get;            ///< The fcntl command that reads the flag word it belongs to.
            int set;            ///< The fcntl command that writes it.
            int bit;            ///< The flag's bit in that word.
        };

        constexpr auto Mappings = std::array {
            FlagMapping { .requested = O_CLOEXEC, .get = F_GETFD, .set = F_SETFD, .bit = FD_CLOEXEC },
            FlagMapping { .requested = O_NONBLOCK, .get = F_GETFL, .set = F_SETFL, .bit = O_NONBLOCK },
        };
        for (auto const& mapping: Mappings)
        {
            if ((flags & mapping.requested) == 0)
                continue;
            for (auto const fd: _pfd)
                if (auto const current = fcntl(fd, mapping.get); current != -1)
                    fcntl(fd, mapping.set, current | mapping.bit);
        }
    #endif
        pipeLog()()("Created pipe: {} {}\n", _pfd[0], _pfd[1]);
    }

    ~PosixPipe() override { close(); }

    PosixPipe(PosixPipe&& other) noexcept: _pfd { other._pfd[0], other._pfd[1] }
    {
        other._pfd[0] = core::platform::InvalidHandle;
        other._pfd[1] = core::platform::InvalidHandle;
    }

    PosixPipe& operator=(PosixPipe&& other) noexcept
    {
        if (this != &other)
        {
            close();
            _pfd[0] = other._pfd[0];
            _pfd[1] = other._pfd[1];
            other._pfd[0] = core::platform::InvalidHandle;
            other._pfd[1] = core::platform::InvalidHandle;
        }
        return *this;
    }

    [[nodiscard]] core::platform::NativeHandle reader() const noexcept override { return _pfd[0]; }

    [[nodiscard]] core::platform::NativeHandle writer() const noexcept override { return _pfd[1]; }

    [[nodiscard]] core::platform::NativeHandle releaseReader() noexcept override
    {
        auto const fd = _pfd[0];
        _pfd[0] = core::platform::InvalidHandle;
        return fd;
    }

    [[nodiscard]] core::platform::NativeHandle releaseWriter() noexcept override
    {
        auto const fd = _pfd[1];
        _pfd[1] = core::platform::InvalidHandle;
        return fd;
    }

    void closeReader() noexcept override { safeClosePipe(&_pfd[0]); }

    void closeWriter() noexcept override { safeClosePipe(&_pfd[1]); }

    [[nodiscard]] bool good() const noexcept override
    {
        return _pfd[0] != core::platform::InvalidHandle && _pfd[1] != core::platform::InvalidHandle;
    }

  private:
    void close()
    {
        closeReader();
        closeWriter();
    }

    core::platform::NativeHandle _pfd[2] { core::platform::InvalidHandle, core::platform::InvalidHandle };
};

std::expected<std::unique_ptr<Pipe>, core::platform::PlatformError> createPipe(unsigned flags)
{
    try
    {
        return std::make_unique<PosixPipe>(flags);
    }
    catch (std::runtime_error const&)
    {
        return std::unexpected(core::platform::PlatformError::PipeCreationFailed);
    }
}

#endif // !_WIN32

} // namespace endo::platform
