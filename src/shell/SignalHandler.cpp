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

#if !defined(__linux__) && !defined(_WIN32)
std::atomic<bool> SignalHandler::_sigchldPending { false };
std::atomic<bool> SignalHandler::_sigtstpPending { false };
std::atomic<bool> SignalHandler::_sigcontPending { false };
#endif

int SignalHandler::initialize(Shell* shell)
{
    _shell = shell;

#if defined(_WIN32)
    // Windows doesn't have POSIX signals
    return -1;
#elif defined(__linux__)
    // Block signals so they can be received via signalfd
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigaddset(&mask, SIGTSTP);
    sigaddset(&mask, SIGCONT);
    sigprocmask(SIG_BLOCK, &mask, nullptr);

    // Ignore SIGTTOU so tcsetpgrp() doesn't stop the shell when transferring terminal control
    signal(SIGTTOU, SIG_IGN);

    // Create signalfd for receiving signals as file descriptor events
    _signalFd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    return _signalFd;
#else
    // macOS/BSD: Use traditional signal handlers
    struct sigaction sa {};
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);

    // SIGCHLD: Child process state change
    sa.sa_handler = sigchldHandler;
    sigaction(SIGCHLD, &sa, nullptr);

    // SIGTSTP: Terminal stop (Ctrl+Z from parent or kill -TSTP)
    sa.sa_handler = sigtstpHandler;
    sigaction(SIGTSTP, &sa, nullptr);

    // SIGCONT: Continue after stop
    sa.sa_handler = sigcontHandler;
    sigaction(SIGCONT, &sa, nullptr);

    // Ignore SIGTTOU so tcsetpgrp() doesn't stop the shell when transferring terminal control
    signal(SIGTTOU, SIG_IGN);

    _sigchldPending.store(false);
    _sigtstpPending.store(false);
    _sigcontPending.store(false);
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

    // Unblock signals
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigaddset(&mask, SIGTSTP);
    sigaddset(&mask, SIGCONT);
    sigprocmask(SIG_UNBLOCK, &mask, nullptr);

    // Restore default handlers
    signal(SIGCHLD, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGCONT, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
#else
    // Restore default signal handlers
    signal(SIGCHLD, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGCONT, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
    _sigchldPending.store(false);
    _sigtstpPending.store(false);
    _sigcontPending.store(false);
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
        switch (info.ssi_signo)
        {
            case SIGCHLD:
                _shell->onSigchld();
                processed = true;
                break;
            case SIGTSTP:
                _shell->onSigtstp();
                processed = true;
                break;
            case SIGCONT:
                _shell->onSigcont();
                processed = true;
                break;
            default: break;
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
        _shell->onSigchld();

    if (_sigtstpPending.exchange(false) && _shell)
        _shell->onSigtstp();

    if (_sigcontPending.exchange(false) && _shell)
        _shell->onSigcont();
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

bool SignalHandler::hasPendingSigtstp() noexcept
{
#if !defined(__linux__) && !defined(_WIN32)
    return _sigtstpPending.load();
#else
    return false;
#endif
}

void SignalHandler::clearPendingSigtstp() noexcept
{
#if !defined(__linux__) && !defined(_WIN32)
    _sigtstpPending.store(false);
#endif
}

bool SignalHandler::hasPendingSigcont() noexcept
{
#if !defined(__linux__) && !defined(_WIN32)
    return _sigcontPending.load();
#else
    return false;
#endif
}

void SignalHandler::clearPendingSigcont() noexcept
{
#if !defined(__linux__) && !defined(_WIN32)
    _sigcontPending.store(false);
#endif
}

void SignalHandler::suspendSelf()
{
#if !defined(_WIN32)
    // Temporarily restore default SIGTSTP handling
    signal(SIGTSTP, SIG_DFL);

    #if defined(__linux__)
    // Unblock SIGTSTP temporarily so we can actually be stopped
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTSTP);
    sigprocmask(SIG_UNBLOCK, &mask, nullptr);
    #endif

    // Re-raise SIGTSTP to actually stop ourselves
    raise(SIGTSTP);

    // When we get here, we've been resumed (SIGCONT was received)

    #if defined(__linux__)
    // Re-block SIGTSTP for signalfd
    sigprocmask(SIG_BLOCK, &mask, nullptr);
    #else
    // Re-install our handler
    struct sigaction sa {};
    sa.sa_handler = sigtstpHandler;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTSTP, &sa, nullptr);
    #endif
#endif
}

#if !defined(__linux__) && !defined(_WIN32)
void SignalHandler::sigchldHandler(int /*sig*/)
{
    _sigchldPending.store(true);
}

void SignalHandler::sigtstpHandler(int /*sig*/)
{
    _sigtstpPending.store(true);
}

void SignalHandler::sigcontHandler(int /*sig*/)
{
    _sigcontPending.store(true);
}
#endif

} // namespace endo
