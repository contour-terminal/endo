// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>

#include "PersistentHistory.hpp"
#include "RequiredPaths.hpp"
#include <platform/testing/InMemoryFileSystem.hpp>

using namespace std::string_literals;
using namespace std::string_view_literals;

namespace
{

/// @brief A history file in a filesystem private to one test case.
///
/// PersistentHistory already takes FileSystem const&, so injecting an in-memory one is the
/// designed seam: each test gets its own tree, with no shared path to collide over and
/// nothing to clean up.
struct HistoryFixture
{
    endo::InMemoryFileSystem fs;
    std::filesystem::path dir { "/test/history" };
    endo::PersistentHistory history { fs };

    HistoryFixture() { history.setFilePath(dir / "history.yml"); }
};

} // namespace

TEST_CASE("PersistentHistory.basic_add_and_search", "[history]")
{
    auto fixture = HistoryFixture {};
    auto& history = fixture.history;

    history.add("git status");
    history.add("git log");
    history.add("cmake --build");

    REQUIRE(history.size() == 3);

    auto const results = history.search("git", 10);
    REQUIRE(results.size() == 2);
    CHECK(results[0] == "git log"); // newest first
    CHECK(results[1] == "git status");
}

TEST_CASE("PersistentHistory.fuzzy_search", "[history]")
{
    auto fixture = HistoryFixture {};
    auto& history = fixture.history;

    history.add("git status");
    history.add("cmake --build --preset clang-debug");

    auto const results = history.searchFuzzy("gst", 10);
    // Should fuzzy-match "git status"
    REQUIRE(!results.empty());
    CHECK(results[0].entry == "git status");
}

TEST_CASE("PersistentHistory.deduplication", "[history]")
{
    auto fixture = HistoryFixture {};
    auto& history = fixture.history;

    history.add("git status");
    history.add("git log");
    history.add("git status"); // duplicate

    REQUIRE(history.size() == 2);
    CHECK(history.richEntries().back().command == "git status"); // moved to end
    CHECK(history.richEntries().back().executionCount == 2);
}

TEST_CASE("PersistentHistory.markLastResult_persists_on_success", "[history]")
{
    auto fixture = HistoryFixture {};
    auto& history = fixture.history;

    history.add("echo hello");
    CHECK(!history.richEntries().back().persisted);

    history.markLastResult(0);
    CHECK(history.richEntries().back().persisted);

    // File should exist after flush
    CHECK(fixture.fs.exists(fixture.dir / "history.yml"));
}

TEST_CASE("PersistentHistory.markLastResult_does_not_persist_new_on_failure", "[history]")
{
    auto fixture = HistoryFixture {};
    auto& history = fixture.history;

    history.add("nonexistent_command");
    CHECK(!history.richEntries().back().persisted);

    history.markLastResult(1);
    CHECK(!history.richEntries().back().persisted);

    // File should NOT exist (no successful command yet)
    CHECK(!fixture.fs.exists(fixture.dir / "history.yml"));
}

TEST_CASE("PersistentHistory.previously_persisted_unpersisted_on_failure", "[history]")
{
    auto fixture = HistoryFixture {};
    auto& history = fixture.history;

    // First: add and succeed
    history.add("git status");
    history.markLastResult(0);
    CHECK(history.richEntries().back().persisted);

    // Run it again but fail
    history.add("git status");
    history.markLastResult(1);

    // Should no longer be persisted after failure
    CHECK(!history.richEntries().back().persisted);
}

TEST_CASE("PersistentHistory.failure_removes_from_disk_on_roundtrip", "[history]")
{
    auto fixture = HistoryFixture {};
    auto const filePath = fixture.dir / "history.yml";

    // Persist two commands successfully
    {
        auto history = endo::PersistentHistory { fixture.fs };
        history.setFilePath(filePath);

        history.add("git status");
        history.markLastResult(0);

        history.add("cmake --build");
        history.markLastResult(0);
    }

    // Re-run "git status" with failure — should un-persist it
    {
        auto history = endo::PersistentHistory { fixture.fs };
        history.setFilePath(filePath);
        history.load();
        REQUIRE(history.size() == 2);

        history.add("git status");
        history.markLastResult(1);
    }

    // Reload and verify only "cmake --build" survives
    {
        auto history = endo::PersistentHistory { fixture.fs };
        history.setFilePath(filePath);
        history.load();

        REQUIRE(history.size() == 1);
        CHECK(history.entries()[0] == "cmake --build");
    }
}

TEST_CASE("PersistentHistory.load_save_roundtrip", "[history]")
{
    auto fixture = HistoryFixture {};
    auto const filePath = fixture.dir / "history.yml";

    // Create and save
    {
        auto history = endo::PersistentHistory { fixture.fs };
        history.setFilePath(filePath);

        history.add("git status");
        history.markLastResult(0);

        history.add("cmake --build");
        history.markLastResult(0);

        history.add("failed_cmd");
        history.markLastResult(1); // not persisted
    }

    // Load in a new instance
    {
        auto history = endo::PersistentHistory { fixture.fs };
        history.setFilePath(filePath);
        history.load();

        // Only 2 persisted entries
        REQUIRE(history.size() == 2);
        CHECK(history.entries()[0] == "git status");
        CHECK(history.entries()[1] == "cmake --build");
    }
}

TEST_CASE("PersistentHistory.recency_over_frequency", "[history]")
{
    auto fixture = HistoryFixture {};
    auto& history = fixture.history;

    // Add "git status" many times to boost frequency
    history.add("git status");
    history.markLastResult(0);
    for (auto i = 0; i < 20; ++i)
    {
        history.add("git status");
        history.markLastResult(0);
    }

    // "git log" added last (most recent)
    history.add("git log");
    history.markLastResult(0);

    auto const results = history.searchFuzzy("git", 10);
    REQUIRE(results.size() == 2);
    // "git log" should score higher due to recency despite lower frequency
    CHECK(results[0].entry == "git log");
}

TEST_CASE("PersistentHistory.frequency_tiebreaker", "[history]")
{
    auto fixture = HistoryFixture {};
    auto& history = fixture.history;

    // Build up frequency for "git status" (executionCount = 20)
    for (auto i = 0; i < 20; ++i)
    {
        history.add("git status");
        history.markLastResult(0);
    }

    // Add many filler entries to dilute recency bonus between adjacent positions
    for (auto i = 0; i < 100; ++i)
    {
        history.add("filler" + std::to_string(i));
        history.markLastResult(0);
    }

    // Add "git log" once (low frequency), then "git status" again so both are at the end
    history.add("git log");
    history.markLastResult(0);
    history.add("git status");
    history.markLastResult(0);

    // Now both "git" entries are the two most recent in a 102-entry list.
    // Recency difference between adjacent entries: ~200/102 ≈ 2
    // Frequency: git status=min(21*2,50)=42, git log=min(1*2,50)=2 → difference=40 >> 2
    // So frequency acts as tiebreaker when recency is nearly equal.
    auto const results = history.searchFuzzy("git", 10);
    REQUIRE(results.size() == 2);
    CHECK(results[0].entry == "git status");
}

TEST_CASE("PersistentHistory.maxSize_eviction", "[history]")
{
    auto fixture = HistoryFixture {};
    auto history = endo::PersistentHistory { fixture.fs, 5 }; // small max
    history.setFilePath(fixture.dir / "history.yml");

    for (auto i = 0; i < 6; ++i)
    {
        history.add("cmd" + std::to_string(i));
        history.markLastResult(0);
    }

    CHECK(history.size() == 5);
}

TEST_CASE("PersistentHistory.import_fish", "[history]")
{
    auto fixture = HistoryFixture {};
    auto const fishPath = fixture.dir / "fish_history";
    fixture.fs.addFile(fishPath,
                       "- cmd: git status\n"
                       "  when: 1707849600\n"
                       "- cmd: cmake --build\n"
                       "  when: 1707849400\n"
                       "- cmd: ls -la\n"
                       "  when: 1707849200\n");

    auto history = endo::PersistentHistory { fixture.fs };
    history.setFilePath(fixture.dir / "history.yml");
    auto const imported = history.importFish(fishPath);
    CHECK(imported == 3);

    REQUIRE(history.size() == 3);
    CHECK(history.entries()[0] == "git status");
    CHECK(history.entries()[1] == "cmake --build");
    CHECK(history.entries()[2] == "ls -la");
    CHECK(history.richEntries()[0].persisted);
}

TEST_CASE("PersistentHistory.import_zsh_extended", "[history]")
{
    auto fixture = HistoryFixture {};
    auto const zshPath = fixture.dir / "zsh_history";
    fixture.fs.addFile(zshPath,
                       ": 1707849600:0;git status\n"
                       ": 1707849400:0;cmake --build\n"
                       ": 1707849200:0;ls -la\n");

    auto history = endo::PersistentHistory { fixture.fs };
    history.setFilePath(fixture.dir / "history.yml");
    auto const imported = history.importZsh(zshPath);
    CHECK(imported == 3);

    REQUIRE(history.size() == 3);
    CHECK(history.entries()[0] == "git status");
    CHECK(history.entries()[2] == "ls -la");
}

TEST_CASE("PersistentHistory.import_bash", "[history]")
{
    auto fixture = HistoryFixture {};
    auto const bashPath = fixture.dir / "bash_history";
    fixture.fs.addFile(bashPath,
                       "git status\n"
                       "cmake --build\n"
                       "ls -la\n");

    auto history = endo::PersistentHistory { fixture.fs };
    history.setFilePath(fixture.dir / "history.yml");
    auto const imported = history.importBash(bashPath);
    CHECK(imported == 3);

    REQUIRE(history.size() == 3);
    CHECK(history.entries()[0] == "git status");
    CHECK(history.entries()[2] == "ls -la");
}

TEST_CASE("PersistentHistory.nonexistent_file_loads_empty", "[history]")
{
    auto fixture = HistoryFixture {};
    auto history = endo::PersistentHistory { fixture.fs };
    history.setFilePath(fixture.dir / "does_not_exist.yml");
    history.load();

    CHECK(history.size() == 0);
    CHECK(history.entries().empty());
}

TEST_CASE("PersistentHistory.corrupt_yaml_loads_empty", "[history]")
{
    auto fixture = HistoryFixture {};
    auto const filePath = fixture.dir / "history.yml";
    fixture.fs.addFile(filePath, "{{{{not valid yaml at all!!!!}}}}");

    auto history = endo::PersistentHistory { fixture.fs };
    history.setFilePath(filePath);
    history.load();

    CHECK(history.size() == 0);
}

TEST_CASE("PersistentHistory.entries_returns_unique_set", "[history]")
{
    auto fixture = HistoryFixture {};
    auto& history = fixture.history;

    history.add("git status");
    history.add("git log");
    history.add("git status"); // re-add

    auto const& entries = history.entries();
    REQUIRE(entries.size() == 2);

    // Check no duplicates
    auto sorted = entries;
    std::ranges::sort(sorted);
    auto const [first, last] = std::ranges::unique(sorted);
    CHECK(first == sorted.end());
}

TEST_CASE("PersistentHistory.trim_whitespace", "[history]")
{
    auto fixture = HistoryFixture {};
    auto& history = fixture.history;

    history.add("  git status  ");
    REQUIRE(history.size() == 1);
    CHECK(history.entries().back() == "git status");

    history.add("\t cmake --build \n");
    REQUIRE(history.size() == 2);
    CHECK(history.entries().back() == "cmake --build");
}

TEST_CASE("PersistentHistory.whitespace_only_not_added", "[history]")
{
    auto fixture = HistoryFixture {};
    auto& history = fixture.history;

    history.add("   ");
    history.add("\t\n\r");
    history.add("");

    CHECK(history.size() == 0);
}

TEST_CASE("PersistentHistory.trimmed_duplicates_detected", "[history]")
{
    auto fixture = HistoryFixture {};
    auto& history = fixture.history;

    history.add("git status");
    history.add("  git status  "); // duplicate after trimming

    REQUIRE(history.size() == 1);
    CHECK(history.richEntries().back().command == "git status");
    CHECK(history.richEntries().back().executionCount == 2);
}

TEST_CASE("PersistentHistory.atomic_flush_uses_tmp", "[history]")
{
    auto fixture = HistoryFixture {};
    auto const filePath = fixture.dir / "history.yml";
    auto history = endo::PersistentHistory { fixture.fs };
    history.setFilePath(filePath);

    history.add("test command");
    history.markLastResult(0);

    // The .tmp file should not remain after flush
    CHECK(!fixture.fs.exists(filePath.string() + ".tmp"));
    // The actual file should exist
    CHECK(fixture.fs.exists(filePath));
}

// ---------------------------------------------------------------------------
// CWD-aware ranking and required-paths validation
// ---------------------------------------------------------------------------

TEST_CASE("PersistentHistory.cwd_and_paths_yaml_roundtrip", "[history][cwd]")
{
    auto fixture = HistoryFixture {};
    auto const filePath = fixture.dir / "history.yml";
    {
        auto history = endo::PersistentHistory { fixture.fs };
        history.setFilePath(filePath);
        history.add("vim ~/notes/plan.md",
                    endo::HistoryAddContext {
                        .cwd = "~/projects/endo",
                        .requiredPaths = { "~/notes/plan.md" },
                    });
        history.markLastResult(0);
    }

    auto history = endo::PersistentHistory { fixture.fs };
    history.setFilePath(filePath);
    history.load();

    REQUIRE(history.size() == 1);
    auto const& entry = history.richEntries().front();
    CHECK(entry.command == "vim ~/notes/plan.md");
    CHECK(entry.cwd == "~/projects/endo");
    REQUIRE(entry.requiredPaths.size() == 1);
    CHECK(entry.requiredPaths.front() == "~/notes/plan.md");
}

TEST_CASE("PersistentHistory.legacy_v1_yaml_loads_without_cwd", "[history][cwd]")
{
    auto fixture = HistoryFixture {};
    auto const filePath = fixture.dir / "history.yml";
    // Hand-craft a legacy v1 file (no cwd, no paths).
    fixture.fs.addFile(filePath,
                       "version: 1\n"
                       "entries:\n"
                       "  - cmd: legacy command\n"
                       "    ts: 1000\n"
                       "    count: 2\n");

    auto history = endo::PersistentHistory { fixture.fs };
    history.setFilePath(filePath);
    history.load();

    REQUIRE(history.size() == 1);
    auto const& entry = history.richEntries().front();
    CHECK(entry.command == "legacy command");
    CHECK(entry.cwd.empty());
    CHECK(entry.requiredPaths.empty());

    // Legacy entries are neither boosted nor penalized by CWD ranking.
    auto options = endo::FuzzySearchOptions {
        .currentCwd = "/home/u/projects/endo",
        .home = "/home/u",
    };
    auto const results = history.searchFuzzy("legacy", 10, options);
    REQUIRE(results.size() == 1);
    CHECK(results.front().entry == "legacy command");
}

TEST_CASE("PersistentHistory.exact_cwd_match_boosts_rank", "[history][cwd]")
{
    auto fixture = HistoryFixture {};
    auto& history = fixture.history;

    history.add("make build", endo::HistoryAddContext { .cwd = "~/projects/other", .requiredPaths = {} });
    history.add("make build", endo::HistoryAddContext { .cwd = "~/projects/endo", .requiredPaths = {} });
    // Duplicate command — add() updates the existing entry. Create a second distinct
    // command to compare ranking.
    history.add("make install", endo::HistoryAddContext { .cwd = "~/projects/other", .requiredPaths = {} });

    auto options = endo::FuzzySearchOptions {
        .currentCwd = "/home/u/projects/endo",
        .home = "/home/u",
    };
    auto const results = history.searchFuzzy("make", 10, options);
    REQUIRE(!results.empty());
    // The first result must be "make build" (our CWD-matching entry), outranking the
    // more-recent but unrelated "make install".
    CHECK(results.front().entry == "make build");
}

TEST_CASE("PersistentHistory.ancestor_cwd_match_weaker_than_exact", "[history][cwd]")
{
    auto fixture = HistoryFixture {};
    auto& history = fixture.history;

    // Entry stored with cwd = ~/projects ; current CWD is ~/projects/endo (descendant).
    history.add("scan project", endo::HistoryAddContext { .cwd = "~/projects", .requiredPaths = {} });
    history.add("scan project exact",
                endo::HistoryAddContext { .cwd = "~/projects/endo", .requiredPaths = {} });

    auto options = endo::FuzzySearchOptions {
        .currentCwd = "/home/u/projects/endo",
        .home = "/home/u",
    };
    auto const results = history.searchFuzzy("scan", 10, options);
    REQUIRE(results.size() == 2);
    // Exact (300) > ancestor (150); the exact match comes first.
    CHECK(results.front().entry == "scan project exact");
    CHECK(results.back().entry == "scan project");
}

TEST_CASE("PersistentHistory.required_paths_filtered_when_missing", "[history][required-paths]")
{
    auto fixture = HistoryFixture {};
    auto& history = fixture.history;

    history.add("cat ~/notes/plan.md",
                endo::HistoryAddContext { .cwd = "~", .requiredPaths = { "~/notes/plan.md" } });
    history.add("ls", endo::HistoryAddContext { .cwd = "~", .requiredPaths = {} });

    auto fs = endo::platform::testing::InMemoryFileSystem {};
    // Note: ~/notes/plan.md is NOT added — the file no longer exists.

    auto options = endo::FuzzySearchOptions {
        .currentCwd = "/home/u",
        .home = "/home/u",
        .validateRequiredPaths = true,
        .fs = &fs,
    };
    auto const results = history.searchFuzzy("", 10, options);
    // Only "ls" (no required paths) should survive validation.
    REQUIRE(results.size() == 1);
    CHECK(results.front().entry == "ls");
}

TEST_CASE("PersistentHistory.required_paths_kept_when_file_exists", "[history][required-paths]")
{
    auto fixture = HistoryFixture {};
    auto& history = fixture.history;

    history.add("cat ~/notes/plan.md",
                endo::HistoryAddContext { .cwd = "~", .requiredPaths = { "~/notes/plan.md" } });

    auto fs = endo::platform::testing::InMemoryFileSystem {};
    fs.addFile("/home/u/notes/plan.md", "contents");

    auto options = endo::FuzzySearchOptions {
        .currentCwd = "/home/u",
        .home = "/home/u",
        .validateRequiredPaths = true,
        .fs = &fs,
    };
    auto const results = history.searchFuzzy("cat", 10, options);
    REQUIRE(results.size() == 1);
    CHECK(results.front().entry == "cat ~/notes/plan.md");
}

TEST_CASE("PersistentHistory.required_paths_validation_disabled", "[history][required-paths]")
{
    auto fixture = HistoryFixture {};
    auto& history = fixture.history;

    history.add("cat ~/missing.txt",
                endo::HistoryAddContext { .cwd = "~", .requiredPaths = { "~/missing.txt" } });

    auto fs = endo::platform::testing::InMemoryFileSystem {};

    auto options = endo::FuzzySearchOptions {
        .currentCwd = "/home/u",
        .home = "/home/u",
        .validateRequiredPaths = false,
        .fs = &fs,
    };
    auto const results = history.searchFuzzy("cat", 10, options);
    REQUIRE(results.size() == 1);
    CHECK(results.front().entry == "cat ~/missing.txt");
}

TEST_CASE("PersistentHistory.fuzzy_finds_substring_after_repeated_grapheme", "[history][fuzzy]")
{
    // Regression: Ctrl+R over history failed to surface an entry when the typed
    // query was a contiguous substring whose leading grapheme also appeared
    // earlier in the command (the greedy matcher scattered the match and the
    // long entry fell below the fuzzy quality threshold). The command has no
    // path arguments, so required-paths validation is not involved here.
    auto fixture = HistoryFixture {};
    auto& history = fixture.history;

    history.add("./build/clangcl-debug/src/shell/endo.exe",
                endo::HistoryAddContext { .cwd = "~/projects/endo", .requiredPaths = {} });

    auto options = endo::FuzzySearchOptions {
        .currentCwd = "/home/u/projects/endo",
        .home = "/home/u",
    };
    auto const results = history.searchFuzzy("endo.exe", 200, options);
    REQUIRE(results.size() == 1);
    CHECK(results.front().entry == "./build/clangcl-debug/src/shell/endo.exe");
}

TEST_CASE("PersistentHistory.readd_updates_cwd_and_paths", "[history][cwd]")
{
    auto fixture = HistoryFixture {};
    auto& history = fixture.history;

    history.add(
        "make build",
        endo::HistoryAddContext { .cwd = "~/projects/a", .requiredPaths = { "~/projects/a/Makefile" } });
    history.add(
        "make build",
        endo::HistoryAddContext { .cwd = "~/projects/b", .requiredPaths = { "~/projects/b/Makefile" } });

    REQUIRE(history.size() == 1);
    auto const& entry = history.richEntries().front();
    CHECK(entry.cwd == "~/projects/b");
    REQUIRE(entry.requiredPaths.size() == 1);
    CHECK(entry.requiredPaths.front() == "~/projects/b/Makefile");
    CHECK(entry.executionCount == 2);
}
