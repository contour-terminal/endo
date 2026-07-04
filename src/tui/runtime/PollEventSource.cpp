// SPDX-License-Identifier: Apache-2.0
#include <tui/runtime/PollEventSource.hpp>

#if defined(_WIN32)
    #include <windows.h>
#else
    #include <tui/runtime/platform/PollHelpers.hpp>

    #include <poll.h>
#endif

namespace tui::runtime
{

#if !defined(_WIN32)

WaitOutcome PollEventSource::wait(int timeoutMs)
{
    auto const& registrations = _registry.registrations();
    static thread_local std::vector<pollfd> fds;
    fds.clear();
    for (auto const& reg: registrations)
        fds.push_back({ .fd = reg.fd, .events = toPollEvents(reg.interest), .revents = 0 });

    auto outcome = WaitOutcome {};

    // Nothing to watch: honour the timeout (a parked timer supplies a finite one) so
    // the runtime's timer can still fire; a negative timeout with no fds would block
    // forever, so report it as a benign timeout instead.
    if (fds.empty())
    {
        if (timeoutMs > 0)
            ::poll(nullptr, 0, timeoutMs);
        return outcome;
    }

    auto const result = ::poll(fds.data(), static_cast<nfds_t>(fds.size()), timeoutMs);
    if (result <= 0)
        // 0: timed out. <0: EINTR or error — re-poll next pump (level-triggered fds
        // re-report readiness); a persistent error surfaces as the fd's HUP below on
        // the next successful poll. Either way, nothing ready this round.
        return outcome;

    for (std::size_t i = 0; i < registrations.size(); ++i)
        routePollRevents(registrations[i].token, fds[i].revents, outcome);
    return outcome;
}

#else // _WIN32

WaitOutcome PollEventSource::wait(int timeoutMs)
{
    auto const& registrations = _registry.registrations();
    static thread_local std::vector<HANDLE> handles;
    handles.clear();
    for (auto const& reg: registrations)
        if (reg.fd != nullptr && reg.fd != endo::platform::InvalidHandle && reg.interest != FdInterest::None)
            handles.push_back(reg.fd);

    auto outcome = WaitOutcome {};

    auto const timeout = (timeoutMs < 0) ? INFINITE : static_cast<DWORD>(timeoutMs);
    if (handles.empty())
    {
        if (timeoutMs > 0)
            Sleep(static_cast<DWORD>(timeoutMs));
        return outcome;
    }

    auto const waitResult =
        WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), FALSE, timeout);
    if (waitResult == WAIT_TIMEOUT || waitResult == WAIT_FAILED)
        return outcome;

    auto const signalled = [](HANDLE handle) {
        return handle != nullptr && WaitForSingleObject(handle, 0) == WAIT_OBJECT_0;
    };
    for (auto const& reg: registrations)
    {
        if (!signalled(reg.fd))
            continue;
        if (hasInterest(reg.interest, FdInterest::Read))
            outcome.readyRead.push_back(reg.token);
        if (hasInterest(reg.interest, FdInterest::Write))
            outcome.readyWrite.push_back(reg.token);
    }
    return outcome;
}

#endif

} // namespace tui::runtime
