// SPDX-License-Identifier: Apache-2.0
#include <tui/runtime/TerminalEventSource.hpp>

#include <algorithm>

namespace tui::runtime
{

// The fd registry is platform-independent: it just maintains the list of
// user-attached descriptors. The per-platform wait() (poll on POSIX,
// WaitForMultipleObjects on Win32) folds this list into its native wait set and
// routes readiness back through WaitOutcome::readyRead / readyWrite. The
// terminal's own sources (input, resize, agent/interrupt wakeups, signal fd)
// stay dedicated members handled directly in wait(), so their typed routing
// (input decode, SIGINT fold, focus activity) is unchanged.

FdToken TerminalEventSource::attach(endo::platform::NativeHandle fd, FdInterest interest)
{
    if (fd == endo::platform::InvalidHandle)
        return FdToken::invalid();
    auto const token = FdToken { ++_nextToken };
    _registrations.push_back(FdRegistration { .token = token, .fd = fd, .interest = interest });
    return token;
}

void TerminalEventSource::updateInterest(FdToken token, FdInterest interest)
{
    auto const it = std::ranges::find(_registrations, token, &FdRegistration::token);
    if (it != _registrations.end())
        it->interest = interest;
}

void TerminalEventSource::detach(FdToken token)
{
    std::erase_if(_registrations, [token](FdRegistration const& reg) { return reg.token == token; });
}

} // namespace tui::runtime
