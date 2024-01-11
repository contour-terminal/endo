// SPDX-License-Identifier: Apache-2.0
module;
#include <fmt/format.h>
#include <crispy/logstore.h>

#include <cstring>
#include <stdexcept>
#include <string>

#include <fcntl.h>
#include <unistd.h>

export module UnixPipe;
namespace endo
{

auto inline pipeLog = logstore::category("pipe  ", "Unix pipe log", logstore::category::state::Enabled);
using namespace std::string_literals;

export inline void saveClose(int* fd) noexcept
{
    if (fd && *fd != -1)
    {
        pipeLog()("Closing fd {}\n", *fd);
        ::close(*fd);
        *fd = -1;
    }
}

export struct UnixPipe
{
    int pfd[2];

    explicit UnixPipe(unsigned flags = 0): pfd { -1, -1 }
    {
#if defined(__linux__)
        if (pipe2(pfd, static_cast<int>(flags)) < 0)
            throw std::runtime_error { "Failed to create PTY pipe. "s + strerror(errno) };
#else
        if (pipe(pfd) < 0)
            throw std::runtime_error { "Failed to create PTY pipe. "s + strerror(errno) };
        for (auto const fd: pfd)
            if (!detail::setFileFlags(fd, flags))
                break;
#endif
        pipeLog()("Created pipe: {} {}\n", pfd[0], pfd[1]);
    }

    inline UnixPipe(UnixPipe&& v) noexcept: pfd { v.pfd[0], v.pfd[1] }
    {
        v.pfd[0] = -1;
        v.pfd[1] = -1;
    }

    UnixPipe& operator=(UnixPipe&& v) noexcept
    {
        close();
        pfd[0] = v.pfd[0];
        pfd[1] = v.pfd[1];
        v.pfd[0] = -1;
        v.pfd[1] = -1;
        return *this;
    }

    UnixPipe(UnixPipe const&) = delete;

    UnixPipe& operator=(UnixPipe const&) = delete;

    ~UnixPipe() { close(); }

    [[nodiscard]] bool good() const noexcept { return pfd[0] != -1 && pfd[1] != -1; }

    [[nodiscard]] int reader() const noexcept { return pfd[0]; }
    [[nodiscard]] int releaseReader() noexcept
    {
        int const fd = pfd[0];
        pfd[0] = -1;
        return fd;
    }
    [[nodiscard]] int writer() const noexcept { return pfd[1]; }

    void closeReader() noexcept { saveClose(&pfd[0]); }
    void closeWriter() noexcept { saveClose(&pfd[1]); }

    void close()
    {
        closeReader();
        closeWriter();
    }
};
} // namespace endo
