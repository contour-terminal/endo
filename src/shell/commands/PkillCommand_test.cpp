// SPDX-License-Identifier: Apache-2.0
#include <shell/commands/PkillCommand.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace endo::pkill_cmd;

namespace
{
std::vector<std::string> makeArgs(std::initializer_list<std::string_view> items)
{
    auto out = std::vector<std::string> {};
    out.reserve(items.size());
    for (auto const& s: items)
        out.emplace_back(s);
    return out;
}
} // namespace

TEST_CASE("parsePkillArgs.missing_pattern", "[pkill]")
{
    auto const args = std::vector<std::string> {};
    auto const result = parsePkillArgs(args);
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("parsePkillArgs.defaults", "[pkill]")
{
    auto const args = makeArgs({ "foo" });
    auto const result = parsePkillArgs(args);
    REQUIRE(result.has_value());
    CHECK(result->pattern == "foo");
    CHECK(result->signal == 15);
    CHECK_FALSE(result->fullMatch);
    CHECK_FALSE(result->exactMatch);
    CHECK_FALSE(result->caseInsensitive);
    CHECK_FALSE(result->countOnly);
    CHECK_FALSE(result->listOnly);
    CHECK_FALSE(result->newestOnly);
    CHECK_FALSE(result->oldestOnly);
    CHECK(result->userFilter.empty());
}

TEST_CASE("parsePkillArgs.numeric_signal_dash_prefix", "[pkill]")
{
    auto const result = parsePkillArgs(makeArgs({ "-9", "foo" }));
    REQUIRE(result.has_value());
    CHECK(result->signal == 9);
    CHECK(result->pattern == "foo");
}

TEST_CASE("parsePkillArgs.named_signal_dash_prefix", "[pkill]")
{
    auto const result = parsePkillArgs(makeArgs({ "-TERM", "foo" }));
    REQUIRE(result.has_value());
    CHECK(result->signal == 15);
    CHECK(result->pattern == "foo");
}

TEST_CASE("parsePkillArgs.sigkill_prefix", "[pkill]")
{
    auto const result = parsePkillArgs(makeArgs({ "-SIGKILL", "foo" }));
    REQUIRE(result.has_value());
    CHECK(result->signal == 9);
}

TEST_CASE("parsePkillArgs.s_flag_signal", "[pkill]")
{
    auto const result = parsePkillArgs(makeArgs({ "-s", "HUP", "foo" }));
    REQUIRE(result.has_value());
    CHECK(result->signal == 1);
    CHECK(result->pattern == "foo");
}

TEST_CASE("parsePkillArgs.s_flag_missing_value", "[pkill]")
{
    auto const result = parsePkillArgs(makeArgs({ "-s" }));
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("parsePkillArgs.unknown_dash_option", "[pkill]")
{
    auto const result = parsePkillArgs(makeArgs({ "-ZZZ", "foo" }));
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("parsePkillArgs.flags_combo", "[pkill]")
{
    auto const result = parsePkillArgs(makeArgs({ "-f", "-x", "-i", "bar" }));
    REQUIRE(result.has_value());
    CHECK(result->fullMatch);
    CHECK(result->exactMatch);
    CHECK(result->caseInsensitive);
    CHECK(result->pattern == "bar");
}

TEST_CASE("parsePkillArgs.list_flag_no_signal_required", "[pkill]")
{
    auto const result = parsePkillArgs(makeArgs({ "-l", "bar" }));
    REQUIRE(result.has_value());
    CHECK(result->listOnly);
    CHECK(result->signal == 15);
}

TEST_CASE("parsePkillArgs.count_flag", "[pkill]")
{
    auto const result = parsePkillArgs(makeArgs({ "-c", "bar" }));
    REQUIRE(result.has_value());
    CHECK(result->countOnly);
}

TEST_CASE("parsePkillArgs.user_filter_single", "[pkill]")
{
    auto const result = parsePkillArgs(makeArgs({ "-u", "alice", "foo" }));
    REQUIRE(result.has_value());
    REQUIRE(result->userFilter.size() == 1);
    CHECK(result->userFilter[0] == "alice");
}

TEST_CASE("parsePkillArgs.user_filter_multi", "[pkill]")
{
    auto const result = parsePkillArgs(makeArgs({ "-u", "alice,bob,carol", "foo" }));
    REQUIRE(result.has_value());
    REQUIRE(result->userFilter.size() == 3);
    CHECK(result->userFilter[0] == "alice");
    CHECK(result->userFilter[1] == "bob");
    CHECK(result->userFilter[2] == "carol");
}

TEST_CASE("parsePkillArgs.user_filter_empty_rejected", "[pkill]")
{
    auto const result = parsePkillArgs(makeArgs({ "-u", ",,", "foo" }));
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("parsePkillArgs.newest_and_oldest_mutually_exclusive", "[pkill]")
{
    auto const result = parsePkillArgs(makeArgs({ "-n", "-o", "foo" }));
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("parsePkillArgs.help_short", "[pkill]")
{
    auto const result = parsePkillArgs(makeArgs({ "-h" }));
    REQUIRE(result.has_value());
    CHECK(result->showHelp);
}

TEST_CASE("parsePkillArgs.help_long", "[pkill]")
{
    auto const result = parsePkillArgs(makeArgs({ "--help" }));
    REQUIRE(result.has_value());
    CHECK(result->showHelp);
}

TEST_CASE("parsePkillArgs.double_dash_pattern", "[pkill]")
{
    auto const result = parsePkillArgs(makeArgs({ "--", "-weird-pattern" }));
    REQUIRE(result.has_value());
    CHECK(result->pattern == "-weird-pattern");
}

TEST_CASE("parsePkillArgs.too_many_positionals", "[pkill]")
{
    auto const result = parsePkillArgs(makeArgs({ "foo", "bar" }));
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("parsePkillArgs.combined_signal_and_flags", "[pkill]")
{
    auto const result = parsePkillArgs(makeArgs({ "-9", "-f", "-i", "my.*proc" }));
    REQUIRE(result.has_value());
    CHECK(result->signal == 9);
    CHECK(result->fullMatch);
    CHECK(result->caseInsensitive);
    CHECK(result->pattern == "my.*proc");
}
