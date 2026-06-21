// SPDX-License-Identifier: Apache-2.0
#include <tui/runtime/TerminalEventSource.hpp>

#if !defined(_WIN32)

    #include <array>
    #include <cerrno>
    #include <cstddef>
    #include <iterator>

    #include <poll.h>

namespace tui::runtime
{

WaitOutcome TerminalEventSource::wait(int timeoutMs)
{
    auto& input = _terminal.input();

    // Build the wait set: input, resize, agent wakeup, interrupt wakeup, signal fd.
    auto fds = std::array<pollfd, 5> {};
    auto count = nfds_t { 0 };

    auto const inputIndex = count;
    fds[count++] = { .fd = input.inputNativeHandle(), .events = POLLIN, .revents = 0 };

    auto resizeIndex = -1;
    if (auto const resizeFd = input.resizeNativeHandle(); resizeFd != endo::platform::InvalidHandle)
    {
        resizeIndex = static_cast<int>(count);
        fds[count++] = { .fd = resizeFd, .events = POLLIN, .revents = 0 };
    }

    auto agentIndex = -1;
    if (_agentWakeup != nullptr)
    {
        agentIndex = static_cast<int>(count);
        fds[count++] = { .fd = _agentWakeup->nativeHandle(), .events = POLLIN, .revents = 0 };
    }

    auto interruptIndex = -1;
    if (_interruptWakeup != nullptr)
    {
        interruptIndex = static_cast<int>(count);
        fds[count++] = { .fd = _interruptWakeup->nativeHandle(), .events = POLLIN, .revents = 0 };
    }

    auto signalIndex = -1;
    if (_signalFd != endo::platform::InvalidHandle)
    {
        signalIndex = static_cast<int>(count);
        fds[count++] = { .fd = _signalFd, .events = POLLIN, .revents = 0 };
    }

    auto const result = ::poll(fds.data(), count, timeoutMs);

    auto outcome = WaitOutcome {};
    if (result == 0)
    {
        outcome.events = input.parserTimeout();
        return finalize(std::move(outcome));
    }
    if (result < 0)
    {
        // EINTR is benign: a signal interrupted the wait; re-poll on the next pump
        // (level-triggered fds re-report any readiness). A persistent error (e.g.
        // EBADF) must not busy-loop — report it as an interrupt so the runtime
        // unwinds the root flow, matching the old REPL's `break` on poll failure.
        if (errno != EINTR)
            outcome.interrupted = true;
        return finalize(std::move(outcome));
    }

    auto const ready = [&](int index) {
        return index >= 0 && (fds[static_cast<std::size_t>(index)].revents & POLLIN) != 0;
    };

    if (ready(resizeIndex))
        if (auto const resize = input.drainResize())
            outcome.events.emplace_back(*resize);

    if (ready(agentIndex))
    {
        _agentWakeup->reset();
        outcome.agentReady = true;
    }

    if (ready(interruptIndex))
        _interruptWakeup->reset();

    if (ready(signalIndex))
        // A reaped job (SIGCHLD) is non-input activity: wake the idle waiter so
        // the owner can report finished jobs promptly instead of at next keypress.
        if (endo::platform::SignalHandler::processSignalFd())
            outcome.activity = true;

    if ((fds[inputIndex].revents & POLLIN) != 0)
    {
        auto parsed = input.readReadyInput();
        if (outcome.events.empty())
            outcome.events = std::move(parsed);
        else
            outcome.events.insert(outcome.events.end(),
                                  std::make_move_iterator(parsed.begin()),
                                  std::make_move_iterator(parsed.end()));
    }

    return finalize(std::move(outcome));
}

} // namespace tui::runtime

#endif // !_WIN32
