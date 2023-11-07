// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <fmt/format.h>

#include <fcntl.h>
#include <unistd.h>

namespace endo
{

inline void saveClose(int* fd) noexcept
{
    if (fd && *fd != -1)
    {
        fmt::print("Closing fd {}\n", *fd);
        ::close(*fd);
        *fd = -1;
    }
}

struct UnixPipe
{
    int pfd[2];

    explicit UnixPipe(unsigned flags = 0);
    UnixPipe(UnixPipe&&) noexcept;
    UnixPipe& operator=(UnixPipe&&) noexcept;
    UnixPipe(UnixPipe const&) = delete;
    UnixPipe& operator=(UnixPipe const&) = delete;
    ~UnixPipe();

    [[nodiscard]] bool good() const noexcept { return pfd[0] != -1 && pfd[1] != -1; }

    [[nodiscard]] int reader() const noexcept { return pfd[0]; }
    [[nodiscard]] int releaseReader() noexcept { int const fd = pfd[0]; pfd[0] = -1; return fd; }
    [[nodiscard]] int writer() const noexcept { return pfd[1]; }

    void closeReader() noexcept;
    void closeWriter() noexcept;

    void close();
};

inline UnixPipe::UnixPipe(UnixPipe&& v) noexcept: pfd { v.pfd[0], v.pfd[1] }
{
    v.pfd[0] = -1;
    v.pfd[1] = -1;
}

inline UnixPipe& UnixPipe::operator=(UnixPipe&& v) noexcept
{
    close();
    pfd[0] = v.pfd[0];
    pfd[1] = v.pfd[1];
    v.pfd[0] = -1;
    v.pfd[1] = -1;
    return *this;
}

inline UnixPipe::~UnixPipe()
{
    close();
}

inline void UnixPipe::close()
{
    closeReader();
    closeWriter();
}

inline void UnixPipe::closeReader() noexcept
{
    saveClose(&pfd[0]);
}

inline void UnixPipe::closeWriter() noexcept
{
    saveClose(&pfd[1]);
}

} // namespace endo
