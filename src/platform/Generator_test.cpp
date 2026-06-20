// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include <platform/Generator.hpp>

using endo::Generator;

namespace
{
/// A coroutine yielding the integers [0, n).
Generator<int> countTo(int n)
{
    for (auto const i: std::views::iota(0, n))
        co_yield i;
}

// Note: exception propagation out of a generator is intentionally NOT tested
// here. `Generator` correctly captures a coroutine-body exception and rethrows
// it to the consumer (verified standalone), but throwing *through a coroutine
// frame* crashes the Catch2 test harness on Windows (an MSVC coroutine-unwind /
// vectored-exception-handler interaction that also affects std::generator). It
// is moot for production: every Generator in this codebase is driven by
// non-throwing iteration (the error_code recursive_directory_iterator overload)
// and std::getline, so no exception ever propagates out of a generator.

/// A coroutine yielding owning strings (verifies non-trivial value types).
Generator<std::string> words()
{
    co_yield std::string("alpha");
    co_yield std::string("beta");
}

Generator<int> infinite()
{
    for (auto const i: std::views::iota(0))
        co_yield i;
}
} // namespace

TEST_CASE("Generator yields every value in order", "[Generator]")
{
    auto collected = std::vector<int> {};
    for (auto const v: countTo(4))
        collected.push_back(v);

    REQUIRE(collected == std::vector<int> { 0, 1, 2, 3 });
}

TEST_CASE("Generator over an empty sequence yields nothing", "[Generator]")
{
    auto count = 0;
    for ([[maybe_unused]] auto const v: countTo(0))
        ++count;

    REQUIRE(count == 0);
}

TEST_CASE("Generator yields owning values", "[Generator]")
{
    auto collected = std::vector<std::string> {};
    for (auto const& w: words())
        collected.push_back(w);

    REQUIRE(collected == std::vector<std::string> { "alpha", "beta" });
}

TEST_CASE("Generator is move-only and the moved-from frame is not double-freed", "[Generator]")
{
    auto gen = countTo(3);
    auto moved = std::move(gen);

    auto collected = std::vector<int> {};
    for (auto const v: moved)
        collected.push_back(v);

    REQUIRE(collected == std::vector<int> { 0, 1, 2 });
}

TEST_CASE("Breaking out of a Generator destroys the suspended frame", "[Generator]")
{
    // An infinite generator that we abandon after a few elements must not hang
    // and must release its frame (verified for leaks under ASAN).
    auto seen = 0;
    for ([[maybe_unused]] auto const v: infinite())
    {
        if (++seen == 5)
            break;
    }
    REQUIRE(seen == 5);
}
