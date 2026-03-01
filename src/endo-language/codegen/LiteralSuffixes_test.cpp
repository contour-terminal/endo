// SPDX-License-Identifier: Apache-2.0
#include <endo-language/TestHelper.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace endo::test;

// =============================================================================
// Size literals — integer suffixes
// =============================================================================

TEST_CASE("LiteralSuffixes.size.bytes", "[literal][size]")
{
    CHECK(executeSourceAndGetOutput("print 42B") == "42 B");
}

TEST_CASE("LiteralSuffixes.size.kilobytes", "[literal][size]")
{
    CHECK(executeSourceAndGetOutput("print 1KB") == "1 KB");
}

TEST_CASE("LiteralSuffixes.size.megabytes", "[literal][size]")
{
    CHECK(executeSourceAndGetOutput("print 5MB") == "5 MB");
}

TEST_CASE("LiteralSuffixes.size.gigabytes", "[literal][size]")
{
    CHECK(executeSourceAndGetOutput("print 2GB") == "2 GB");
}

TEST_CASE("LiteralSuffixes.size.terabytes", "[literal][size]")
{
    CHECK(executeSourceAndGetOutput("print 1TB") == "1 TB");
}

// =============================================================================
// Size literals — float suffixes
// =============================================================================

TEST_CASE("LiteralSuffixes.size.float_kilobytes", "[literal][size]")
{
    // 3.5 * 1024 = 3584 bytes = 3.5 KB
    CHECK(executeSourceAndGetOutput("print 3.5KB") == "3.5 KB");
}

TEST_CASE("LiteralSuffixes.size.float_megabytes", "[literal][size]")
{
    // 1.5 * 1024^2 = 1572864 bytes = 1.5 MB
    CHECK(executeSourceAndGetOutput("print 1.5MB") == "1.5 MB");
}

TEST_CASE("LiteralSuffixes.size.float_bytes_whole", "[literal][size]")
{
    // 1.0B is allowed (whole number)
    CHECK(executeSourceAndGetOutput("print 1.0B") == "1 B");
}

// =============================================================================
// Size literals — legacy underscore syntax (backward compat)
// =============================================================================

TEST_CASE("LiteralSuffixes.size.legacy_underscore_KB", "[literal][size]")
{
    CHECK(executeSourceAndGetOutput("print 1_KB") == "1 KB");
}

TEST_CASE("LiteralSuffixes.size.legacy_underscore_MB", "[literal][size]")
{
    CHECK(executeSourceAndGetOutput("print 5_MB") == "5 MB");
}

// =============================================================================
// TimeSpan literals — integer suffixes
// =============================================================================

TEST_CASE("LiteralSuffixes.timespan.milliseconds", "[literal][timespan]")
{
    CHECK(executeSourceAndGetOutput("print 100ms") == "100ms");
}

TEST_CASE("LiteralSuffixes.timespan.seconds", "[literal][timespan]")
{
    CHECK(executeSourceAndGetOutput("print 5s") == "5s");
}

TEST_CASE("LiteralSuffixes.timespan.minutes", "[literal][timespan]")
{
    CHECK(executeSourceAndGetOutput("print 2min") == "2m");
}

TEST_CASE("LiteralSuffixes.timespan.hours", "[literal][timespan]")
{
    CHECK(executeSourceAndGetOutput("print 1h") == "1h");
}

// =============================================================================
// TimeSpan literals — float suffixes
// =============================================================================

TEST_CASE("LiteralSuffixes.timespan.float_hours", "[literal][timespan]")
{
    // 1.5 * 3600000 = 5400000 ms = 1h 30m
    CHECK(executeSourceAndGetOutput("print 1.5h") == "1h 30m");
}

TEST_CASE("LiteralSuffixes.timespan.float_seconds", "[literal][timespan]")
{
    // 2.5 * 1000 = 2500 ms = 2s 500ms
    CHECK(executeSourceAndGetOutput("print 2.5s") == "2s 500ms");
}

TEST_CASE("LiteralSuffixes.timespan.float_ms_whole", "[literal][timespan]")
{
    // 1.0ms is allowed (whole number)
    CHECK(executeSourceAndGetOutput("print 1.0ms") == "1ms");
}

// =============================================================================
// Let bindings with literal suffixes
// =============================================================================

TEST_CASE("LiteralSuffixes.let_binding_size", "[literal][size]")
{
    CHECK(executeSourceAndGetOutput("let x = 5KB\nprint x") == "5 KB");
}

TEST_CASE("LiteralSuffixes.let_binding_timespan", "[literal][timespan]")
{
    CHECK(executeSourceAndGetOutput("let t = 2min\nprint t") == "2m");
}

// =============================================================================
// Comparison with literal suffixes
// =============================================================================

TEST_CASE("LiteralSuffixes.size.comparison", "[literal][size]")
{
    CHECK(executeSourceAndGetOutput("print (1KB < 1MB)") == "true");
}

TEST_CASE("LiteralSuffixes.timespan.comparison", "[literal][timespan]")
{
    CHECK(executeSourceAndGetOutput("print (1s < 1min)") == "true");
}

// =============================================================================
// Field access on literal suffixes
// =============================================================================

TEST_CASE("LiteralSuffixes.size.field_access", "[literal][size]")
{
    CHECK(executeSourceAndGetOutput("let s = 1KB\nprint s.bytes") == "1024");
}

TEST_CASE("LiteralSuffixes.timespan.field_access", "[literal][timespan]")
{
    CHECK(executeSourceAndGetOutput("let t = 5s\nprint t.milliseconds") == "5000");
}
