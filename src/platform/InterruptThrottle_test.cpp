// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <platform/InterruptThrottle.hpp>
#include <platform/SignalHandler.hpp>

using endo::platform::InterruptThrottle;
using endo::platform::SignalHandler;

namespace
{
/// Ensures the global pending-SIGINT flag is clear before and after a test so
/// cases do not leak interrupt state into one another.
struct SigintGuard
{
    SigintGuard() { SignalHandler::clearPendingSigint(); }

    ~SigintGuard() { SignalHandler::clearPendingSigint(); }
};
} // namespace

TEST_CASE("InterruptThrottle reports no interrupt when none is pending", "[InterruptThrottle]")
{
    auto const _ = SigintGuard {};
    auto throttle = InterruptThrottle { 1 };

    for (auto i = 0; i < 10; ++i)
        REQUIRE_FALSE(throttle.pending());
}

TEST_CASE("InterruptThrottle detects and consumes a pending interrupt", "[InterruptThrottle]")
{
    auto const _ = SigintGuard {};
    auto throttle = InterruptThrottle { 1 };

    SignalHandler::simulateSigint();
    REQUIRE(throttle.pending());

    // The flag is consumed, so a subsequent poll is clean.
    REQUIRE_FALSE(SignalHandler::hasPendingSigint());
    REQUIRE_FALSE(throttle.pending());
}

TEST_CASE("InterruptThrottle only polls every `interval` calls", "[InterruptThrottle]")
{
    auto const _ = SigintGuard {};
    auto throttle = InterruptThrottle { 4 };

    SignalHandler::simulateSigint();

    // The first three calls do not reach a poll boundary, so they ignore the
    // pending interrupt (and must leave the flag untouched).
    REQUIRE_FALSE(throttle.pending());
    REQUIRE_FALSE(throttle.pending());
    REQUIRE_FALSE(throttle.pending());
    REQUIRE(SignalHandler::hasPendingSigint());

    // The fourth call polls and observes the interrupt.
    REQUIRE(throttle.pending());
    REQUIRE_FALSE(SignalHandler::hasPendingSigint());
}
