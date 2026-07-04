// SPDX-License-Identifier: Apache-2.0
#include <tui/runtime/TerminalEventSource.hpp>

#if !defined(_WIN32)

    #include <tui/runtime/platform/PollHelpers.hpp>

    #include <cerrno>
    #include <cstddef>
    #include <iterator>
    #include <vector>

    #include <poll.h>

namespace tui::runtime
{

WaitOutcome TerminalEventSource::wait(int timeoutMs)
{
    auto& input = _terminal.input();

    // Build the wait set: the terminal's own sources (input, resize, agent wakeup,
    // interrupt wakeup, signal fd) followed by any user-registered fds. The known
    // sources are tracked by index for their typed routing; user fds are routed by
    // token into readyRead / readyWrite. The buffer is thread_local and cleared
    // (capacity kept) so this hot pump path is allocation-free in steady state.
    static thread_local std::vector<pollfd> fds;
    fds.clear();
    auto const& registrations = _registry.registrations();

    auto const inputIndex = fds.size();
    fds.push_back({ .fd = input.inputNativeHandle(), .events = POLLIN, .revents = 0 });

    auto resizeIndex = -1;
    if (auto const resizeFd = input.resizeNativeHandle(); resizeFd != endo::platform::InvalidHandle)
    {
        resizeIndex = static_cast<int>(fds.size());
        fds.push_back({ .fd = resizeFd, .events = POLLIN, .revents = 0 });
    }

    auto agentIndex = -1;
    if (_agentWakeup != nullptr)
    {
        agentIndex = static_cast<int>(fds.size());
        fds.push_back({ .fd = _agentWakeup->nativeHandle(), .events = POLLIN, .revents = 0 });
    }

    auto interruptIndex = -1;
    if (_interruptWakeup != nullptr)
    {
        interruptIndex = static_cast<int>(fds.size());
        fds.push_back({ .fd = _interruptWakeup->nativeHandle(), .events = POLLIN, .revents = 0 });
    }

    auto signalIndex = -1;
    if (_signalFd != endo::platform::InvalidHandle)
    {
        signalIndex = static_cast<int>(fds.size());
        fds.push_back({ .fd = _signalFd, .events = POLLIN, .revents = 0 });
    }

    // The first registrationBase entries are the known sources; the rest map 1:1
    // onto the registry, in order.
    auto const registrationBase = fds.size();
    for (auto const& reg: registrations)
        fds.push_back({ .fd = reg.fd, .events = toPollEvents(reg.interest), .revents = 0 });

    auto const result = ::poll(fds.data(), static_cast<nfds_t>(fds.size()), timeoutMs);

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

    // The input terminal closed (EOF / not a TTY / detached): poll reports POLLHUP or
    // POLLERR with no POLLIN, and none of the readers below would consume it — leaving the
    // pump to re-poll instantly and busy-loop. Treat it as an interrupt so the runtime
    // unwinds the root flow cleanly instead of spinning at 100% CPU.
    if ((fds[inputIndex].revents & (POLLHUP | POLLERR | POLLNVAL)) != 0
        && (fds[inputIndex].revents & POLLIN) == 0)
    {
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

    // Route user-registered fds via the shared helper (HUP/ERR resolve a read
    // waiter too, so the caller observes EOF rather than the pump spinning).
    for (std::size_t i = 0; i < registrations.size(); ++i)
        routePollRevents(registrations[i].token, fds[registrationBase + i].revents, outcome);

    return finalize(std::move(outcome));
}

} // namespace tui::runtime

#endif // !_WIN32
