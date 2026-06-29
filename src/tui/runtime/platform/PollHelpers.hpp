// SPDX-License-Identifier: Apache-2.0
#pragma once

#if !defined(_WIN32)

    #include <tui/runtime/EventSource.hpp>

    #include <vector>

    #include <poll.h>

namespace tui::runtime
{

/// Translates a readiness interest mask into poll(2) event bits.
/// @param interest The interest to translate.
/// @return The corresponding POLLIN/POLLOUT bitmask.
[[nodiscard]] inline short toPollEvents(FdInterest interest) noexcept
{
    short events = 0;
    if (hasInterest(interest, FdInterest::Read))
        events |= POLLIN;
    if (hasInterest(interest, FdInterest::Write))
        events |= POLLOUT;
    return events;
}

/// Routes a registered fd's poll(2) revents into a wait outcome's ready-token
/// lists. Read-readiness includes HUP/ERR/NVAL so a parked reader is resumed to
/// observe EOF rather than the pump spinning.
/// @param token The registration's token.
/// @param revents The revents poll(2) reported for the fd.
/// @param outcome The outcome to append the token to (readyRead / readyWrite).
inline void routePollRevents(FdToken token, short revents, WaitOutcome& outcome)
{
    if ((revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL)) != 0)
        outcome.readyRead.push_back(token);
    if ((revents & POLLOUT) != 0)
        outcome.readyWrite.push_back(token);
}

} // namespace tui::runtime

#endif // !_WIN32
