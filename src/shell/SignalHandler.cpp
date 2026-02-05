// SPDX-License-Identifier: Apache-2.0
#include "SignalHandler.hpp"

#include <csignal>

#include "Shell.hpp"

#if !defined(_WIN32)
    #include <unistd.h>
    #if defined(__linux__)
        #include <sys/signalfd.h>
    #endif
#endif

namespace endo
{

Shell* SignalHandler::_shell = nullptr;
int SignalHandler::_signalFd = -1;

#if !defined(__linux__)
std::atomic<bool> SignalHandler::_sigchldPending { false };
#endif

int SignalHandler::initialize(Shell* shell)
{
    _shell = shell;

#if defined(_WIN32)
    // Windows doesn't have POSIX signals
    return -1;
#elif defined(__linux__)
    // Block SIGCHLD so it can be received via signalfd
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, nullptr);

    // Create signalfd for receiving signals as file descriptor events
    _signalFd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    return _signalFd;
#else
    // macOS/BSD: Use traditional signal handler
    struct sigaction sa {};
    sa.sa_handler = sigchldHandler;
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGCHLD, &sa, nullptr);

    _sigchldPending.store(false);
    return -1;
#endif
}

void SignalHandler::restore()
{
#if defined(_WIN32)
    // Nothing to restore on Windows
#elif defined(__linux__)
    if (_signalFd >= 0)
    {
        close(_signalFd);
        _signalFd = -1;
    }

    // Unblock SIGCHLD
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_UNBLOCK, &mask, nullptr);

    // Restore default handler
    signal(SIGCHLD, SIG_DFL);
#else
    // Restore default signal handler
    signal(SIGCHLD, SIG_DFL);
    _sigchldPending.store(false);
#endif

    _shell = nullptr;
}

int SignalHandler::signalFd() noexcept
{
    return _signalFd;
}

bool SignalHandler::processSignalFd()
{
#if defined(__linux__)
    if (_signalFd < 0 || !_shell)
        return false;

    signalfd_siginfo info;
    bool processed = false;

    // Read all pending signals from signalfd
    while (read(_signalFd, &info, sizeof(info)) == sizeof(info))
    {
        if (info.ssi_signo == SIGCHLD)
        {
            _shell->onSigchld();
            processed = true;
        }
    }
    return processed;
#else
    return false;
#endif
}

void SignalHandler::processPendingSignals()
{
#if !defined(__linux__) && !defined(_WIN32)
    if (_sigchldPending.exchange(false) && _shell)
    {
        _shell->onSigchld();
    }
#endif
}

bool SignalHandler::hasPendingSigchld() noexcept
{
#if !defined(__linux__) && !defined(_WIN32)
    return _sigchldPending.load();
#else
    return false;
#endif
}

void SignalHandler::clearPendingSigchld() noexcept
{
#if !defined(__linux__) && !defined(_WIN32)
    _sigchldPending.store(false);
#endif
}

#if !defined(__linux__) && !defined(_WIN32)
void SignalHandler::sigchldHandler(int /*sig*/)
{
    _sigchldPending.store(true);
}
#endif

} // namespace endo
