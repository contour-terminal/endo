// SPDX-License-Identifier: Apache-2.0
#include <shell/commands/PgrepCommand.hpp>
#include <shell/commands/ProcessMatch.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace endo::pgrep_cmd;
using endo::ProcessEntry;
using endo::process_match::filterProcesses;
using endo::process_match::MatchOptions;

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

ProcessEntry makeEntry(int64_t pid, std::string command, std::string user = "alice")
{
    return ProcessEntry { .pid = pid,
                          .ppid = 1,
                          .user = std::move(user),
                          .cpuPercent = 0.0,
                          .memKb = 0,
                          .command = std::move(command) };
}

std::vector<ProcessEntry> sampleEntries()
{
    return {
        makeEntry(100, "/usr/bin/sleep"),
        makeEntry(200, "bash", "bob"),
        makeEntry(300, "sleepwalker"),
        makeEntry(400, "Sleep"),
    };
}

std::vector<int64_t> pidsOf(std::vector<ProcessEntry> const& entries)
{
    auto pids = std::vector<int64_t> {};
    pids.reserve(entries.size());
    for (auto const& entry: entries)
        pids.push_back(entry.pid);
    return pids;
}

} // namespace

// ---------------------------------------------------------------------------
// parsePgrepArgs
// ---------------------------------------------------------------------------

TEST_CASE("parsePgrepArgs.missing_pattern", "[pgrep]")
{
    auto const result = parsePgrepArgs(std::vector<std::string> {});
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("parsePgrepArgs.defaults", "[pgrep]")
{
    auto const result = parsePgrepArgs(makeArgs({ "foo" }));
    REQUIRE(result.has_value());
    CHECK(result->pattern == "foo");
    CHECK(result->delimiter == "\n");
    CHECK_FALSE(result->fullMatch);
    CHECK_FALSE(result->exactMatch);
    CHECK_FALSE(result->caseInsensitive);
    CHECK_FALSE(result->invert);
    CHECK_FALSE(result->countOnly);
    CHECK_FALSE(result->listName);
    CHECK_FALSE(result->newestOnly);
    CHECK_FALSE(result->oldestOnly);
    CHECK(result->userFilter.empty());
}

TEST_CASE("parsePgrepArgs.flags_combo", "[pgrep]")
{
    auto const result = parsePgrepArgs(makeArgs({ "-f", "-x", "-i", "-v", "-c", "-l", "bar" }));
    REQUIRE(result.has_value());
    CHECK(result->fullMatch);
    CHECK(result->exactMatch);
    CHECK(result->caseInsensitive);
    CHECK(result->invert);
    CHECK(result->countOnly);
    CHECK(result->listName);
    CHECK(result->pattern == "bar");
}

TEST_CASE("parsePgrepArgs.delimiter", "[pgrep]")
{
    auto const result = parsePgrepArgs(makeArgs({ "-d", ",", "foo" }));
    REQUIRE(result.has_value());
    CHECK(result->delimiter == ",");
}

TEST_CASE("parsePgrepArgs.delimiter_missing_value", "[pgrep]")
{
    auto const result = parsePgrepArgs(makeArgs({ "foo", "-d" }));
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("parsePgrepArgs.user_filter", "[pgrep]")
{
    auto const result = parsePgrepArgs(makeArgs({ "-u", "alice,bob", "foo" }));
    REQUIRE(result.has_value());
    REQUIRE(result->userFilter.size() == 2);
    CHECK(result->userFilter[0] == "alice");
    CHECK(result->userFilter[1] == "bob");
}

TEST_CASE("parsePgrepArgs.newest_and_oldest_mutually_exclusive", "[pgrep]")
{
    auto const result = parsePgrepArgs(makeArgs({ "-n", "-o", "foo" }));
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("parsePgrepArgs.unknown_option_rejected", "[pgrep]")
{
    auto const result = parsePgrepArgs(makeArgs({ "-Z", "foo" }));
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("parsePgrepArgs.too_many_positionals", "[pgrep]")
{
    auto const result = parsePgrepArgs(makeArgs({ "foo", "bar" }));
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("parsePgrepArgs.help", "[pgrep]")
{
    for (auto const* const flag: { "-h", "--help" })
    {
        auto const result = parsePgrepArgs(makeArgs({ flag }));
        REQUIRE(result.has_value());
        CHECK(result->showHelp);
    }
}

TEST_CASE("parsePgrepArgs.double_dash_pattern", "[pgrep]")
{
    auto const result = parsePgrepArgs(makeArgs({ "--", "-weird-pattern" }));
    REQUIRE(result.has_value());
    CHECK(result->pattern == "-weird-pattern");
}

// ---------------------------------------------------------------------------
// process_match::filterProcesses
// ---------------------------------------------------------------------------

TEST_CASE("filterProcesses.substring_search", "[pgrep]")
{
    auto const entries = sampleEntries();
    auto const result = filterProcesses(entries, MatchOptions { .pattern = "sleep" });
    REQUIRE(result.has_value());
    CHECK(pidsOf(*result) == std::vector<int64_t> { 100, 300 });
}

TEST_CASE("filterProcesses.exact_match_anchored", "[pgrep]")
{
    auto const entries = std::vector<ProcessEntry> { makeEntry(1, "sleep"), makeEntry(2, "sleepwalker") };
    auto const result = filterProcesses(entries, MatchOptions { .pattern = "sleep", .exactMatch = true });
    REQUIRE(result.has_value());
    CHECK(pidsOf(*result) == std::vector<int64_t> { 1 });
}

TEST_CASE("filterProcesses.case_insensitive", "[pgrep]")
{
    auto const entries = sampleEntries();
    auto const result = filterProcesses(
        entries, MatchOptions { .pattern = "sleep", .exactMatch = true, .caseInsensitive = true });
    REQUIRE(result.has_value());
    CHECK(pidsOf(*result) == std::vector<int64_t> { 400 });
}

TEST_CASE("filterProcesses.invert", "[pgrep]")
{
    auto const entries = sampleEntries();
    auto const result = filterProcesses(entries, MatchOptions { .pattern = "sleep", .invert = true });
    REQUIRE(result.has_value());
    CHECK(pidsOf(*result) == std::vector<int64_t> { 200, 400 });
}

TEST_CASE("filterProcesses.user_filter", "[pgrep]")
{
    auto const entries = sampleEntries();
    auto const result = filterProcesses(entries, MatchOptions { .pattern = ".", .userFilter = { "bob" } });
    REQUIRE(result.has_value());
    CHECK(pidsOf(*result) == std::vector<int64_t> { 200 });
}

TEST_CASE("filterProcesses.newest_and_oldest", "[pgrep]")
{
    auto const entries = sampleEntries();

    auto const newest = filterProcesses(entries, MatchOptions { .pattern = "sleep", .newestOnly = true });
    REQUIRE(newest.has_value());
    CHECK(pidsOf(*newest) == std::vector<int64_t> { 300 });

    auto const oldest = filterProcesses(entries, MatchOptions { .pattern = "sleep", .oldestOnly = true });
    REQUIRE(oldest.has_value());
    CHECK(pidsOf(*oldest) == std::vector<int64_t> { 100 });
}

TEST_CASE("filterProcesses.exclude_pid", "[pgrep]")
{
    auto const entries = sampleEntries();
    auto const result = filterProcesses(entries, MatchOptions { .pattern = "sleep", .excludePid = 100 });
    REQUIRE(result.has_value());
    CHECK(pidsOf(*result) == std::vector<int64_t> { 300 });
}

TEST_CASE("filterProcesses.invalid_pattern", "[pgrep]")
{
    auto const entries = sampleEntries();
    auto const result = filterProcesses(entries, MatchOptions { .pattern = "([" });
    REQUIRE_FALSE(result.has_value());
}
