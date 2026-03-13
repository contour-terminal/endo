// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <optional>

namespace tui
{

/// Computes remaining milliseconds from a start time point and a timeout duration.
///
/// @param start The time point when the timer started.
/// @param timeout The total timeout duration.
/// @return 0 if the timeout has expired, otherwise the remaining milliseconds.
template <typename Duration>
[[nodiscard]] int remainingMs(std::chrono::steady_clock::time_point start, Duration timeout)
{
    auto const remaining = timeout - (std::chrono::steady_clock::now() - start);
    if (remaining <= std::chrono::milliseconds::zero())
        return 0;
    return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count());
}

/// Computes remaining milliseconds for an optional timer.
///
/// @param start The optional time point when the timer started.
/// @param timeout The total timeout duration.
/// @return -1 if the timer is not active, 0 if expired, otherwise the remaining milliseconds.
template <typename Duration>
[[nodiscard]] int remainingMs(std::optional<std::chrono::steady_clock::time_point> const& start,
                              Duration timeout)
{
    if (!start)
        return -1;
    return remainingMs(*start, timeout);
}

} // namespace tui
