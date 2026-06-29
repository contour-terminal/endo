// SPDX-License-Identifier: Apache-2.0
#include <tui/runtime/PollEventSource.hpp>

#include <algorithm>

#if defined(_WIN32)
    #include <windows.h>
#else
    #include <poll.h>
#endif

namespace tui::runtime
{

FdToken PollEventSource::attach(endo::platform::NativeHandle fd, FdInterest interest)
{
    if (fd == endo::platform::InvalidHandle)
        return FdToken::invalid();
    auto const token = FdToken { ++_nextToken };
    _registrations.push_back(FdRegistration { .token = token, .fd = fd, .interest = interest });
    return token;
}

void PollEventSource::updateInterest(FdToken token, FdInterest interest)
{
    auto const it = std::ranges::find(_registrations, token, &FdRegistration::token);
    if (it != _registrations.end())
        it->interest = interest;
}

void PollEventSource::detach(FdToken token)
{
    std::erase_if(_registrations, [token](FdRegistration const& reg) { return reg.token == token; });
}

#if !defined(_WIN32)

namespace
{
    /// Translates a readiness interest mask into poll(2) event bits.
    [[nodiscard]] short toPollEvents(FdInterest interest) noexcept
    {
        short events = 0;
        if (hasInterest(interest, FdInterest::Read))
            events |= POLLIN;
        if (hasInterest(interest, FdInterest::Write))
            events |= POLLOUT;
        return events;
    }
} // namespace

WaitOutcome PollEventSource::wait(int timeoutMs)
{
    auto fds = std::vector<pollfd> {};
    fds.reserve(_registrations.size());
    for (auto const& reg: _registrations)
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

    for (std::size_t i = 0; i < _registrations.size(); ++i)
    {
        auto const& reg = _registrations[i];
        auto const revents = fds[i].revents;
        if ((revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL)) != 0)
            outcome.readyRead.push_back(reg.token);
        if ((revents & POLLOUT) != 0)
            outcome.readyWrite.push_back(reg.token);
    }
    return outcome;
}

#else // _WIN32

WaitOutcome PollEventSource::wait(int timeoutMs)
{
    auto handles = std::vector<HANDLE> {};
    handles.reserve(_registrations.size());
    for (auto const& reg: _registrations)
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
    for (auto const& reg: _registrations)
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
