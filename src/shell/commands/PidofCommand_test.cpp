// SPDX-License-Identifier: Apache-2.0
#include <shell/commands/PidofCommand.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace endo::pidof_cmd;
using endo::ProcessEntry;

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

ProcessEntry makeEntry(int64_t pid, std::string command)
{
    return ProcessEntry {
        .pid = pid, .ppid = 1, .user = "alice", .cpuPercent = 0.0, .memKb = 0, .command = std::move(command)
    };
}

constexpr auto PosixPolicy = NameMatchPolicy {};
constexpr auto WindowsPolicy = NameMatchPolicy { .caseInsensitive = true, .stripExeSuffix = true };

} // namespace

// ---------------------------------------------------------------------------
// parsePidofArgs
// ---------------------------------------------------------------------------

TEST_CASE("parsePidofArgs.missing_program_name", "[pidof]")
{
    auto const result = parsePidofArgs(std::vector<std::string> {});
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("parsePidofArgs.defaults", "[pidof]")
{
    auto const result = parsePidofArgs(makeArgs({ "sleep" }));
    REQUIRE(result.has_value());
    REQUIRE(result->programNames.size() == 1);
    CHECK(result->programNames[0] == "sleep");
    CHECK(result->separator == " ");
    CHECK(result->omitPids.empty());
    CHECK_FALSE(result->singleShot);
    CHECK_FALSE(result->quiet);
    CHECK_FALSE(result->showHelp);
}

TEST_CASE("parsePidofArgs.multiple_program_names", "[pidof]")
{
    auto const result = parsePidofArgs(makeArgs({ "sleep", "bash", "vim" }));
    REQUIRE(result.has_value());
    REQUIRE(result->programNames.size() == 3);
    CHECK(result->programNames[1] == "bash");
}

TEST_CASE("parsePidofArgs.single_shot_and_quiet", "[pidof]")
{
    auto const result = parsePidofArgs(makeArgs({ "-s", "-q", "sleep" }));
    REQUIRE(result.has_value());
    CHECK(result->singleShot);
    CHECK(result->quiet);
}

TEST_CASE("parsePidofArgs.separator_variants", "[pidof]")
{
    for (auto const* const flag: { "-S", "--separator", "-d" })
    {
        auto const result = parsePidofArgs(makeArgs({ flag, ",", "sleep" }));
        REQUIRE(result.has_value());
        CHECK(result->separator == ",");
    }
}

TEST_CASE("parsePidofArgs.separator_missing_value", "[pidof]")
{
    auto const result = parsePidofArgs(makeArgs({ "sleep", "-S" }));
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("parsePidofArgs.omit_pids_comma_separated_and_repeated", "[pidof]")
{
    auto const result = parsePidofArgs(makeArgs({ "-o", "1,2", "-o", "3", "sleep" }));
    REQUIRE(result.has_value());
    REQUIRE(result->omitPids.size() == 3);
    CHECK(result->omitPids[0] == 1);
    CHECK(result->omitPids[1] == 2);
    CHECK(result->omitPids[2] == 3);
}

TEST_CASE("parsePidofArgs.omit_pids_non_numeric_rejected", "[pidof]")
{
    auto const result = parsePidofArgs(makeArgs({ "-o", "abc", "sleep" }));
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("parsePidofArgs.omit_pids_empty_rejected", "[pidof]")
{
    auto const result = parsePidofArgs(makeArgs({ "-o", ",,", "sleep" }));
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("parsePidofArgs.unknown_option_rejected", "[pidof]")
{
    auto const result = parsePidofArgs(makeArgs({ "-Z", "sleep" }));
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("parsePidofArgs.help", "[pidof]")
{
    for (auto const* const flag: { "-h", "--help" })
    {
        auto const result = parsePidofArgs(makeArgs({ flag }));
        REQUIRE(result.has_value());
        CHECK(result->showHelp);
    }
}

TEST_CASE("parsePidofArgs.double_dash_allows_dashed_names", "[pidof]")
{
    auto const result = parsePidofArgs(makeArgs({ "--", "-weird-name" }));
    REQUIRE(result.has_value());
    REQUIRE(result->programNames.size() == 1);
    CHECK(result->programNames[0] == "-weird-name");
}

// ---------------------------------------------------------------------------
// matchesProgramName
// ---------------------------------------------------------------------------

TEST_CASE("matchesProgramName.exact_command", "[pidof]")
{
    CHECK(matchesProgramName(makeEntry(1, "sleep"), "sleep", PosixPolicy));
    CHECK_FALSE(matchesProgramName(makeEntry(1, "sleepd"), "sleep", PosixPolicy));
    CHECK_FALSE(matchesProgramName(makeEntry(1, "sleep"), "slee", PosixPolicy));
}

TEST_CASE("matchesProgramName.basename_of_full_path", "[pidof]")
{
    auto const entry = makeEntry(1, "/usr/bin/sleep");
    CHECK(matchesProgramName(entry, "sleep", PosixPolicy));
    CHECK(matchesProgramName(entry, "/usr/bin/sleep", PosixPolicy));
    CHECK_FALSE(matchesProgramName(entry, "/bin/sleep", PosixPolicy));
}

TEST_CASE("matchesProgramName.posix_is_case_sensitive", "[pidof]")
{
    CHECK_FALSE(matchesProgramName(makeEntry(1, "Sleep"), "sleep", PosixPolicy));
}

TEST_CASE("matchesProgramName.windows_case_insensitive_exe", "[pidof]")
{
    auto const entry = makeEntry(1, "notepad.exe");
    CHECK(matchesProgramName(entry, "notepad", WindowsPolicy));
    CHECK(matchesProgramName(entry, "NOTEPAD.EXE", WindowsPolicy));
    CHECK(matchesProgramName(entry, "Notepad.exe", WindowsPolicy));
    CHECK_FALSE(matchesProgramName(entry, "notepad", PosixPolicy));
}

TEST_CASE("matchesProgramName.windows_backslash_basename", "[pidof]")
{
    auto const entry = makeEntry(1, R"(C:\Windows\notepad.exe)");
    CHECK(matchesProgramName(entry, "notepad", WindowsPolicy));
    CHECK(matchesProgramName(entry, "notepad.exe", WindowsPolicy));
}

TEST_CASE("matchesProgramName.exe_suffix_not_stripped_on_posix", "[pidof]")
{
    CHECK_FALSE(matchesProgramName(makeEntry(1, "wine.exe"), "wine", PosixPolicy));
    CHECK(matchesProgramName(makeEntry(1, "wine.exe"), "wine.exe", PosixPolicy));
}

// ---------------------------------------------------------------------------
// findPids
// ---------------------------------------------------------------------------

namespace
{

std::vector<ProcessEntry> sampleEntries()
{
    return {
        makeEntry(100, "/usr/bin/sleep"),
        makeEntry(200, "bash"),
        makeEntry(300, "/usr/bin/sleep"),
        makeEntry(400, "/usr/bin/vim"),
    };
}

PidofOptions optionsFor(std::initializer_list<std::string_view> names)
{
    auto opts = PidofOptions {};
    for (auto const& name: names)
        opts.programNames.emplace_back(name);
    return opts;
}

} // namespace

TEST_CASE("findPids.sorted_descending", "[pidof]")
{
    auto const entries = sampleEntries();
    auto const pids = findPids(entries, optionsFor({ "sleep" }), PosixPolicy);
    REQUIRE(pids.size() == 2);
    CHECK(pids[0] == 300);
    CHECK(pids[1] == 100);
}

TEST_CASE("findPids.multiple_names", "[pidof]")
{
    auto const entries = sampleEntries();
    auto const pids = findPids(entries, optionsFor({ "sleep", "vim" }), PosixPolicy);
    REQUIRE(pids.size() == 3);
    CHECK(pids[0] == 400);
    CHECK(pids[1] == 300);
    CHECK(pids[2] == 100);
}

TEST_CASE("findPids.no_match", "[pidof]")
{
    auto const entries = sampleEntries();
    CHECK(findPids(entries, optionsFor({ "nonexistent" }), PosixPolicy).empty());
}

TEST_CASE("findPids.single_shot_keeps_newest", "[pidof]")
{
    auto const entries = sampleEntries();
    auto opts = optionsFor({ "sleep" });
    opts.singleShot = true;
    auto const pids = findPids(entries, opts, PosixPolicy);
    REQUIRE(pids.size() == 1);
    CHECK(pids[0] == 300);
}

TEST_CASE("findPids.omit_pids", "[pidof]")
{
    auto const entries = sampleEntries();
    auto opts = optionsFor({ "sleep" });
    opts.omitPids = { 300 };
    auto const pids = findPids(entries, opts, PosixPolicy);
    REQUIRE(pids.size() == 1);
    CHECK(pids[0] == 100);
}

TEST_CASE("findPids.windows_policy", "[pidof]")
{
    auto const entries = std::vector<ProcessEntry> {
        makeEntry(10, "notepad.exe"),
        makeEntry(20, "EXPLORER.EXE"),
    };
    CHECK(findPids(entries, optionsFor({ "notepad" }), WindowsPolicy) == std::vector<int64_t> { 10 });
    CHECK(findPids(entries, optionsFor({ "explorer" }), WindowsPolicy) == std::vector<int64_t> { 20 });
    CHECK(findPids(entries, optionsFor({ "notepad" }), PosixPolicy).empty());
}
