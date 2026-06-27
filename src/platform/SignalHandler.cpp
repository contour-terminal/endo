// SPDX-License-Identifier: Apache-2.0
#include "SignalHandler.hpp"

#include <csignal>

#include <platform/Wakeup.hpp>

#if defined(_WIN32)
    #include <windows.h>
#else
    #include <unistd.h>
    #if defined(__linux__)
        #include <sys/signalfd.h>
    #endif
#endif

namespace endo::platform
{

SignalCallback* SignalHandler::_callback = nullptr;
int SignalHandler::_signalFd = -1;
std::atomic<bool> SignalHandler::_sigintPending { false };
std::atomic<Wakeup*> SignalHandler::_interruptWakeup { nullptr };

#if !defined(__linux__) && !defined(_WIN32)
std::atomic<bool> SignalHandler::_sigChldPending { false };
std::atomic<bool> SignalHandler::_sigTstpPending { false };
std::atomic<bool> SignalHandler::_sigContPending { false };
#endif

int SignalHandler::initialize(SignalCallback* callback)
{
    _callback = callback;

#if defined(_WIN32)
    // Windows has no POSIX signals. Install a console control handler so that
    // Ctrl+C / Ctrl+Break interrupt the foreground child instead of terminating
    // the shell. The handler records a pending interrupt and returns TRUE,
    // preventing the default handler from calling ExitProcess on the shell.
    SetConsoleCtrlHandler(&SignalHandler::consoleCtrlHandler, TRUE);
    return -1;
#elif defined(__linux__)
    // Block signals so they can be received via signalfd
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigaddset(&mask, SIGTSTP);
    sigaddset(&mask, SIGCONT);
    sigaddset(&mask, SIGINT);
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

    // Handle SIGINT via flag — builtins (like sleep) poll this to support Ctrl+C interruption.
    // At the prompt, terminal is in raw mode so Ctrl+C is a keypress, not a signal.
    sa.sa_handler = sigintHandler;
    sigaction(SIGINT, &sa, nullptr);

    _sigChldPending.store(false);
    _sigTstpPending.store(false);
    _sigContPending.store(false);
    _sigintPending.store(false);
    return -1;
#endif
}

void SignalHandler::restore()
{
#if defined(_WIN32)
    // Deregister the console control handler installed in initialize() and clear any
    // interrupt it may have recorded, so a pending flag does not survive shell teardown
    // into a later SignalHandler user.
    SetConsoleCtrlHandler(&SignalHandler::consoleCtrlHandler, FALSE);
    _sigintPending.store(false);
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
    sigaddset(&mask, SIGINT);
    sigprocmask(SIG_UNBLOCK, &mask, nullptr);

    // Restore default handlers
    signal(SIGCHLD, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGCONT, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
    signal(SIGINT, SIG_DFL);
#else
    // Restore default signal handlers
    signal(SIGCHLD, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGCONT, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
    signal(SIGINT, SIG_DFL);
    _sigChldPending.store(false);
    _sigTstpPending.store(false);
    _sigContPending.store(false);
    _sigintPending.store(false);
#endif

    _callback = nullptr;
}

int SignalHandler::signalFd() noexcept
{
    return _signalFd;
}

bool SignalHandler::processSignalFd()
{
#if defined(__linux__)
    if (_signalFd < 0 || !_callback)
        return false;

    signalfd_siginfo info {};
    bool processed = false;

    // Read all pending signals from signalfd
    while (read(_signalFd, &info, sizeof(info)) == sizeof(info))
    {
        switch (info.ssi_signo)
        {
            case SIGCHLD:
                _callback->onSigchld();
                processed = true;
                break;
            case SIGTSTP:
                _callback->onSigtstp();
                processed = true;
                break;
            case SIGCONT:
                _callback->onSigcont();
                processed = true;
                break;
            case SIGINT:
                _sigintPending.store(true);
                signalInterruptWakeup();
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
    if (_sigChldPending.exchange(false) && _callback)
        _callback->onSigchld();

    if (_sigTstpPending.exchange(false) && _callback)
        _callback->onSigtstp();

    if (_sigContPending.exchange(false) && _callback)
        _callback->onSigcont();
#endif
}

bool SignalHandler::hasPendingSigchld() noexcept
{
#if !defined(__linux__) && !defined(_WIN32)
    return _sigChldPending.load();
#else
    return false;
#endif
}

void SignalHandler::clearPendingSigchld() noexcept
{
#if !defined(__linux__) && !defined(_WIN32)
    _sigChldPending.store(false);
#endif
}

bool SignalHandler::hasPendingSigtstp() noexcept
{
#if !defined(__linux__) && !defined(_WIN32)
    return _sigTstpPending.load();
#else
    return false;
#endif
}

void SignalHandler::clearPendingSigtstp() noexcept
{
#if !defined(__linux__) && !defined(_WIN32)
    _sigTstpPending.store(false);
#endif
}

bool SignalHandler::hasPendingSigcont() noexcept
{
#if !defined(__linux__) && !defined(_WIN32)
    return _sigContPending.load();
#else
    return false;
#endif
}

void SignalHandler::clearPendingSigcont() noexcept
{
#if !defined(__linux__) && !defined(_WIN32)
    _sigContPending.store(false);
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
    _sigChldPending.store(true);
}

void SignalHandler::sigtstpHandler(int /*sig*/)
{
    _sigTstpPending.store(true);
}

void SignalHandler::sigcontHandler(int /*sig*/)
{
    _sigContPending.store(true);
}

void SignalHandler::sigintHandler(int /*sig*/)
{
    _sigintPending.store(true);
    signalInterruptWakeup();
}
#endif

bool SignalHandler::hasPendingSigint() noexcept
{
    return _sigintPending.load();
}

void SignalHandler::clearPendingSigint() noexcept
{
    _sigintPending.store(false);
}

void SignalHandler::setInterruptWakeup(Wakeup* wakeup) noexcept
{
    _interruptWakeup.store(wakeup);
}

void SignalHandler::signalInterruptWakeup() noexcept
{
    // Loaded atomically: registration may race with a handler/console thread.
    if (auto* const wakeup = _interruptWakeup.load())
        wakeup->signal();
}

void SignalHandler::simulateSigint() noexcept
{
    _sigintPending.store(true);
}

bool SignalHandler::isInterruptCtrlEvent(unsigned long ctrlType) noexcept
{
#if defined(_WIN32)
    return ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT;
#else
    // Mirror the Win32 CTRL_C_EVENT (0) and CTRL_BREAK_EVENT (1) values so the
    // interrupt policy can be unit-tested on any platform.
    constexpr unsigned long CtrlCEvent = 0;
    constexpr unsigned long CtrlBreakEvent = 1;
    return ctrlType == CtrlCEvent || ctrlType == CtrlBreakEvent;
#endif
}

#if defined(_WIN32)
int __stdcall SignalHandler::consoleCtrlHandler(unsigned long ctrlType)
{
    if (isInterruptCtrlEvent(ctrlType))
    {
        // Record the interrupt so in-process builtins polling hasPendingSigint()
        // can abort, and return TRUE so the shell is not terminated. The
        // foreground child shares the console process group and receives its own
        // CTRL_C_EVENT from the OS; background jobs are shielded by being created
        // in a separate console process group.
        _sigintPending.store(true);
        signalInterruptWakeup();
        return TRUE;
    }
    return FALSE;
}
#endif

} // namespace endo::platform
