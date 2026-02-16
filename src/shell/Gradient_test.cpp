// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <array>

#include "Gradient.hpp"

using tui::operator""_rgb;

TEST_CASE("multiStopGradient.empty_stops_returns_black", "[gradient]")
{
    auto const result = endo::multiStopGradient({}, 0.5f);
    CHECK(result.r == 0);
    CHECK(result.g == 0);
    CHECK(result.b == 0);
}

TEST_CASE("multiStopGradient.single_stop_returns_that_stop", "[gradient]")
{
    auto const stop = 0xFF8040_rgb;
    auto const stops = std::array<tui::RgbColor, 1> { stop };
    CHECK(endo::multiStopGradient(stops, 0.0f).r == stop.r);
    CHECK(endo::multiStopGradient(stops, 0.5f).g == stop.g);
    CHECK(endo::multiStopGradient(stops, 1.0f).b == stop.b);
}

TEST_CASE("multiStopGradient.two_stops_matches_lerp", "[gradient]")
{
    auto const a = 0x000000_rgb;
    auto const b = 0xFF00FF_rgb;
    auto const stops = std::array<tui::RgbColor, 2> { a, b };

    // t=0 → a
    auto const r0 = endo::multiStopGradient(stops, 0.0f);
    CHECK(r0.r == 0);
    CHECK(r0.g == 0);
    CHECK(r0.b == 0);

    // t=1 → b
    auto const r1 = endo::multiStopGradient(stops, 1.0f);
    CHECK(r1.r == 0xFF);
    CHECK(r1.g == 0);
    CHECK(r1.b == 0xFF);

    // t=0.5 → midpoint
    auto const rMid = endo::multiStopGradient(stops, 0.5f);
    auto const expected = endo::lerpColor(a, b, 0.5f);
    CHECK(rMid.r == expected.r);
    CHECK(rMid.g == expected.g);
    CHECK(rMid.b == expected.b);
}

TEST_CASE("multiStopGradient.five_stops_boundaries", "[gradient]")
{
    auto const stops = std::array<tui::RgbColor, 5> {
        0x252545_rgb, // 0
        0x1E3840_rgb, // 1
        0x1E3828_rgb, // 2
        0x352040_rgb, // 3
        0x252545_rgb, // 4
    };

    // t=0 → first stop
    auto const r0 = endo::multiStopGradient(stops, 0.0f);
    CHECK(r0.r == stops[0].r);
    CHECK(r0.g == stops[0].g);
    CHECK(r0.b == stops[0].b);

    // t=1 → last stop
    auto const r1 = endo::multiStopGradient(stops, 1.0f);
    CHECK(r1.r == stops[4].r);
    CHECK(r1.g == stops[4].g);
    CHECK(r1.b == stops[4].b);

    // t=0.25 → exactly stop[1]
    auto const r25 = endo::multiStopGradient(stops, 0.25f);
    CHECK(r25.r == stops[1].r);
    CHECK(r25.g == stops[1].g);
    CHECK(r25.b == stops[1].b);

    // t=0.5 → exactly stop[2]
    auto const r50 = endo::multiStopGradient(stops, 0.5f);
    CHECK(r50.r == stops[2].r);
    CHECK(r50.g == stops[2].g);
    CHECK(r50.b == stops[2].b);

    // t=0.75 → exactly stop[3]
    auto const r75 = endo::multiStopGradient(stops, 0.75f);
    CHECK(r75.r == stops[3].r);
    CHECK(r75.g == stops[3].g);
    CHECK(r75.b == stops[3].b);
}

TEST_CASE("multiStopGradient.five_stops_midpoint", "[gradient]")
{
    auto const stops = std::array<tui::RgbColor, 5> {
        0x252545_rgb, 0x1E3840_rgb, 0x1E3828_rgb, 0x352040_rgb, 0x252545_rgb,
    };

    // t=0.125 → midpoint between stops[0] and stops[1]
    auto const r = endo::multiStopGradient(stops, 0.125f);
    auto const expected = endo::lerpColor(stops[0], stops[1], 0.5f);
    CHECK(r.r == expected.r);
    CHECK(r.g == expected.g);
    CHECK(r.b == expected.b);
}

TEST_CASE("multiStopGradient.clamps_out_of_range", "[gradient]")
{
    auto const a = 0x102030_rgb;
    auto const b = 0x405060_rgb;
    auto const stops = std::array<tui::RgbColor, 2> { a, b };

    // t < 0 → clamps to first stop
    auto const rNeg = endo::multiStopGradient(stops, -1.0f);
    CHECK(rNeg.r == a.r);
    CHECK(rNeg.g == a.g);
    CHECK(rNeg.b == a.b);

    // t > 1 → clamps to last stop
    auto const rOver = endo::multiStopGradient(stops, 2.0f);
    CHECK(rOver.r == b.r);
    CHECK(rOver.g == b.g);
    CHECK(rOver.b == b.b);
}

TEST_CASE("lerpColor.basic", "[gradient]")
{
    auto const black = tui::RgbColor { 0, 0, 0 };
    auto const white = tui::RgbColor { 255, 255, 255 };

    auto const mid = endo::lerpColor(black, white, 0.5f);
    // 127 or 128 depending on rounding — allow both
    CHECK(mid.r >= 127);
    CHECK(mid.r <= 128);

    auto const start = endo::lerpColor(black, white, 0.0f);
    CHECK(start.r == 0);

    auto const end = endo::lerpColor(black, white, 1.0f);
    CHECK(end.r == 255);
}
