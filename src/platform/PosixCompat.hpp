// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file PosixCompat.hpp
/// @brief The POSIX names that endo's process and redirection code uses, on every platform.
///
/// On POSIX this header only includes the system headers that declare them. Windows has the CRT's
/// `<fcntl.h>` and `<io.h>`, which provide the `O_*` flags and `_mktemp_s`. It has neither the
/// standard-descriptor numbers nor the job-control signals, so this header defines them there with
/// their POSIX values. endo's own `<platform/Types.hpp>` used to do this for every includer.
/// `core::platform`'s `Types.hpp` deliberately does not, so each file that uses these names
/// includes this header.

#if defined(_WIN32)
    #include <csignal>

    #include <fcntl.h>
    #include <io.h>

    #ifndef STDIN_FILENO
        #define STDIN_FILENO 0
    #endif
    #ifndef STDOUT_FILENO
        #define STDOUT_FILENO 1
    #endif
    #ifndef STDERR_FILENO
        #define STDERR_FILENO 2
    #endif
    #ifndef SIGKILL
        #define SIGKILL 9
    #endif
    #ifndef SIGCHLD
        #define SIGCHLD 17
    #endif
    #ifndef SIGCONT
        #define SIGCONT 18
    #endif
    #ifndef SIGTSTP
        #define SIGTSTP 20
    #endif
#else
    #include <sys/types.h>

    #include <fcntl.h>
    #include <unistd.h>
#endif
