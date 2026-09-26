// SPDX-License-Identifier: Apache-2.0
#include <core/platform/SignalHandler.hpp>

#include <catch2/catch_test_macros.hpp>

#include <ranges>

#include <platform/InterruptThrottle.hpp>

using core::platform::SignalHandler;
using endo::platform::InterruptThrottle;

namespace
{
/// Ensures the global pending-SIGINT flag is clear before and after a test so
/// cases do not leak interrupt state into one another.
struct SigintGuard
{
    SigintGuard() { core::platform::SignalHandler::clearPendingSigint(); }

    ~SigintGuard() { core::platform::SignalHandler::clearPendingSigint(); }
};
} // namespace

TEST_CASE("InterruptThrottle reports no interrupt when none is pending", "[InterruptThrottle]")
{
    auto const _ = SigintGuard {};
    auto throttle = InterruptThrottle { 1 };

    for ([[maybe_unused]] auto const i: std::views::iota(0, 10))
        REQUIRE_FALSE(throttle.pending());
}

TEST_CASE("InterruptThrottle detects and consumes a pending interrupt", "[InterruptThrottle]")
{
    auto const _ = SigintGuard {};
    auto throttle = InterruptThrottle { 1 };

    core::platform::SignalHandler::simulateSigint();
    REQUIRE(throttle.pending());

    // The flag is consumed, so a subsequent poll is clean.
    REQUIRE_FALSE(core::platform::SignalHandler::hasPendingSigint());
    REQUIRE_FALSE(throttle.pending());
}

TEST_CASE("InterruptThrottle polls on the first call and then every `interval` calls", "[InterruptThrottle]")
{
    auto const _ = SigintGuard {};
    auto throttle = InterruptThrottle { 4 };

    // The very first call polls immediately (so short loops are never poll-blind);
    // with no interrupt pending it returns false.
    REQUIRE_FALSE(throttle.pending());

    core::platform::SignalHandler::simulateSigint();

    // The next three calls are between poll boundaries, so they ignore the pending
    // interrupt and leave the flag untouched.
    REQUIRE_FALSE(throttle.pending());
    REQUIRE_FALSE(throttle.pending());
    REQUIRE_FALSE(throttle.pending());
    REQUIRE(core::platform::SignalHandler::hasPendingSigint());

    // The next call reaches the interval boundary, polls, and observes the interrupt.
    REQUIRE(throttle.pending());
    REQUIRE_FALSE(core::platform::SignalHandler::hasPendingSigint());
}
