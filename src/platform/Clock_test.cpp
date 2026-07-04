// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>

#include <platform/Clock.hpp>

using namespace std::chrono_literals;
using endo::platform::ManualClock;
using endo::platform::SteadyClock;
using endo::platform::SteadyTimePoint;

TEST_CASE("SteadyClock::now is monotonic", "[clock]")
{
    auto const clock = SteadyClock {};
    auto const first = clock.now();
    auto const second = clock.now();
    REQUIRE(second >= first);
}

TEST_CASE("ManualClock starts at the constructed value and does not advance on its own", "[clock]")
{
    auto const start = SteadyTimePoint {} + 1s;
    auto const clock = ManualClock { start };
    REQUIRE(clock.now() == start);
    // Reading twice yields the same value: time is frozen until advance/setNow.
    REQUIRE(clock.now() == start);
}

TEST_CASE("ManualClock::advance moves time forward by the delta", "[clock]")
{
    auto clock = ManualClock {};
    auto const origin = clock.now();
    clock.advance(100ms);
    REQUIRE(clock.now() == origin + 100ms);
    clock.advance(50ms);
    REQUIRE(clock.now() == origin + 150ms);
}

TEST_CASE("ManualClock::setNow hard-sets the value", "[clock]")
{
    auto clock = ManualClock { SteadyTimePoint {} + 5s };
    auto const target = SteadyTimePoint {} + 2s;
    clock.setNow(target);
    REQUIRE(clock.now() == target);
}

TEST_CASE("defaultSteadyClock returns a usable singleton", "[clock]")
{
    auto& a = endo::platform::defaultSteadyClock();
    auto& b = endo::platform::defaultSteadyClock();
    REQUIRE(&a == &b);
    // Sample in a defined order (the macro's argument evaluation order is
    // unspecified, so two inline now() calls could otherwise read out of order).
    auto const first = a.now();
    auto const second = b.now();
    REQUIRE(second >= first);
}
