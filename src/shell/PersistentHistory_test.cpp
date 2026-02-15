// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "PersistentHistory.hpp"

using namespace std::string_literals;
using namespace std::string_view_literals;

namespace
{

/// @brief RAII helper that creates a temporary directory and removes it on destruction.
struct TempDir
{
    std::filesystem::path path;

    TempDir()
    {
        path = std::filesystem::current_path() / "tmp" / "history_test";
        std::filesystem::create_directories(path);
    }

    ~TempDir() { std::filesystem::remove_all(path); }
};

void writeFile(std::filesystem::path const& path, std::string_view content)
{
    std::filesystem::create_directories(path.parent_path());
    auto ofs = std::ofstream(path);
    ofs << content;
}

} // namespace

TEST_CASE("PersistentHistory.basic_add_and_search", "[history]")
{
    auto dir = TempDir {};
    auto history = endo::PersistentHistory {};
    history.setFilePath(dir.path / "history.yml");

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
    auto dir = TempDir {};
    auto history = endo::PersistentHistory {};
    history.setFilePath(dir.path / "history.yml");

    history.add("git status");
    history.add("cmake --build --preset clang-debug");

    auto const results = history.searchFuzzy("gst", 10);
    // Should fuzzy-match "git status"
    REQUIRE(!results.empty());
    CHECK(results[0].entry == "git status");
}

TEST_CASE("PersistentHistory.deduplication", "[history]")
{
    auto dir = TempDir {};
    auto history = endo::PersistentHistory {};
    history.setFilePath(dir.path / "history.yml");

    history.add("git status");
    history.add("git log");
    history.add("git status"); // duplicate

    REQUIRE(history.size() == 2);
    CHECK(history.richEntries().back().command == "git status"); // moved to end
    CHECK(history.richEntries().back().executionCount == 2);
}

TEST_CASE("PersistentHistory.markLastResult_persists_on_success", "[history]")
{
    auto dir = TempDir {};
    auto history = endo::PersistentHistory {};
    history.setFilePath(dir.path / "history.yml");

    history.add("echo hello");
    CHECK(!history.richEntries().back().persisted);

    history.markLastResult(0);
    CHECK(history.richEntries().back().persisted);

    // File should exist after flush
    CHECK(std::filesystem::exists(dir.path / "history.yml"));
}

TEST_CASE("PersistentHistory.markLastResult_does_not_persist_new_on_failure", "[history]")
{
    auto dir = TempDir {};
    auto history = endo::PersistentHistory {};
    history.setFilePath(dir.path / "history.yml");

    history.add("nonexistent_command");
    CHECK(!history.richEntries().back().persisted);

    history.markLastResult(1);
    CHECK(!history.richEntries().back().persisted);

    // File should NOT exist (no successful command yet)
    CHECK(!std::filesystem::exists(dir.path / "history.yml"));
}

TEST_CASE("PersistentHistory.previously_persisted_unpersisted_on_failure", "[history]")
{
    auto dir = TempDir {};
    auto history = endo::PersistentHistory {};
    history.setFilePath(dir.path / "history.yml");

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
    auto dir = TempDir {};
    auto const filePath = dir.path / "history.yml";

    // Persist two commands successfully
    {
        auto history = endo::PersistentHistory {};
        history.setFilePath(filePath);

        history.add("git status");
        history.markLastResult(0);

        history.add("cmake --build");
        history.markLastResult(0);
    }

    // Re-run "git status" with failure — should un-persist it
    {
        auto history = endo::PersistentHistory {};
        history.setFilePath(filePath);
        history.load();
        REQUIRE(history.size() == 2);

        history.add("git status");
        history.markLastResult(1);
    }

    // Reload and verify only "cmake --build" survives
    {
        auto history = endo::PersistentHistory {};
        history.setFilePath(filePath);
        history.load();

        REQUIRE(history.size() == 1);
        CHECK(history.entries()[0] == "cmake --build");
    }
}

TEST_CASE("PersistentHistory.load_save_roundtrip", "[history]")
{
    auto dir = TempDir {};
    auto const filePath = dir.path / "history.yml";

    // Create and save
    {
        auto history = endo::PersistentHistory {};
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
        auto history = endo::PersistentHistory {};
        history.setFilePath(filePath);
        history.load();

        // Only 2 persisted entries
        REQUIRE(history.size() == 2);
        CHECK(history.entries()[0] == "git status");
        CHECK(history.entries()[1] == "cmake --build");
    }
}

TEST_CASE("PersistentHistory.frequency_aware_scoring", "[history]")
{
    auto dir = TempDir {};
    auto history = endo::PersistentHistory {};
    history.setFilePath(dir.path / "history.yml");

    // Add "git status" many times to boost frequency
    history.add("git status");
    history.markLastResult(0);
    for (auto i = 0; i < 20; ++i)
    {
        history.add("git status");
        history.markLastResult(0);
    }

    history.add("git log");
    history.markLastResult(0);

    auto const results = history.searchFuzzy("git", 10);
    REQUIRE(results.size() == 2);
    // "git status" should score higher due to frequency despite being older
    CHECK(results[0].entry == "git status");
}

TEST_CASE("PersistentHistory.maxSize_eviction", "[history]")
{
    auto dir = TempDir {};
    auto history = endo::PersistentHistory(5); // small max
    history.setFilePath(dir.path / "history.yml");

    for (auto i = 0; i < 6; ++i)
    {
        history.add("cmd" + std::to_string(i));
        history.markLastResult(0);
    }

    CHECK(history.size() == 5);
}

TEST_CASE("PersistentHistory.import_fish", "[history]")
{
    auto dir = TempDir {};
    auto const fishPath = dir.path / "fish_history";
    writeFile(fishPath,
              "- cmd: git status\n"
              "  when: 1707849600\n"
              "- cmd: cmake --build\n"
              "  when: 1707849400\n"
              "- cmd: ls -la\n"
              "  when: 1707849200\n");

    auto history = endo::PersistentHistory {};
    history.setFilePath(dir.path / "history.yml");
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
    auto dir = TempDir {};
    auto const zshPath = dir.path / "zsh_history";
    writeFile(zshPath,
              ": 1707849600:0;git status\n"
              ": 1707849400:0;cmake --build\n"
              ": 1707849200:0;ls -la\n");

    auto history = endo::PersistentHistory {};
    history.setFilePath(dir.path / "history.yml");
    auto const imported = history.importZsh(zshPath);
    CHECK(imported == 3);

    REQUIRE(history.size() == 3);
    CHECK(history.entries()[0] == "git status");
    CHECK(history.entries()[2] == "ls -la");
}

TEST_CASE("PersistentHistory.import_bash", "[history]")
{
    auto dir = TempDir {};
    auto const bashPath = dir.path / "bash_history";
    writeFile(bashPath,
              "git status\n"
              "cmake --build\n"
              "ls -la\n");

    auto history = endo::PersistentHistory {};
    history.setFilePath(dir.path / "history.yml");
    auto const imported = history.importBash(bashPath);
    CHECK(imported == 3);

    REQUIRE(history.size() == 3);
    CHECK(history.entries()[0] == "git status");
    CHECK(history.entries()[2] == "ls -la");
}

TEST_CASE("PersistentHistory.nonexistent_file_loads_empty", "[history]")
{
    auto dir = TempDir {};
    auto history = endo::PersistentHistory {};
    history.setFilePath(dir.path / "does_not_exist.yml");
    history.load();

    CHECK(history.size() == 0);
    CHECK(history.entries().empty());
}

TEST_CASE("PersistentHistory.corrupt_yaml_loads_empty", "[history]")
{
    auto dir = TempDir {};
    auto const filePath = dir.path / "history.yml";
    writeFile(filePath, "{{{{not valid yaml at all!!!!}}}}");

    auto history = endo::PersistentHistory {};
    history.setFilePath(filePath);
    history.load();

    CHECK(history.size() == 0);
}

TEST_CASE("PersistentHistory.entries_returns_unique_set", "[history]")
{
    auto dir = TempDir {};
    auto history = endo::PersistentHistory {};
    history.setFilePath(dir.path / "history.yml");

    history.add("git status");
    history.add("git log");
    history.add("git status"); // re-add

    auto const& entries = history.entries();
    REQUIRE(entries.size() == 2);

    // Check no duplicates
    auto sorted = entries;
    std::sort(sorted.begin(), sorted.end());
    auto const it = std::unique(sorted.begin(), sorted.end());
    CHECK(it == sorted.end());
}

TEST_CASE("PersistentHistory.atomic_flush_uses_tmp", "[history]")
{
    auto dir = TempDir {};
    auto const filePath = dir.path / "history.yml";
    auto history = endo::PersistentHistory {};
    history.setFilePath(filePath);

    history.add("test command");
    history.markLastResult(0);

    // The .tmp file should not remain after flush
    CHECK(!std::filesystem::exists(filePath.string() + ".tmp"));
    // The actual file should exist
    CHECK(std::filesystem::exists(filePath));
}
