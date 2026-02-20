// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <memory>

#include <agent/commands/FilePathCompleter.hpp>

using namespace endo::agent;

namespace
{

/// Helper to create a completer with sample project files and directories.
/// Mirrors the format produced by ProjectFileTree::filePaths() where
/// directories have a trailing '/' suffix.
std::unique_ptr<FilePathCompleter> createTestCompleter()
{
    auto completer = std::make_unique<FilePathCompleter>();
    completer->setFilePaths({
        "CMakeLists.txt",
        "README.md",
        "src/",
        "src/agent/",
        "src/agent/FilePathCompleter.cpp",
        "src/agent/FilePathCompleter.hpp",
        "src/main.cpp",
        "src/shell/",
        "src/shell/Shell.cpp",
    });
    return completer;
}

} // namespace

TEST_CASE("FilePathCompleter.at_alone_returns_all_entries", "[agent][filepath][completer]")
{
    auto completer = createTestCompleter();

    auto const items = completer->complete("@", 1);
    CHECK(items.size() == 9); // 6 files + 3 directories
}

TEST_CASE("FilePathCompleter.prefix_match_src", "[agent][filepath][completer]")
{
    auto completer = createTestCompleter();

    auto const items = completer->complete("@src", 4);
    // src/, src/agent/, src/agent/FilePathCompleter.{cpp,hpp}, src/main.cpp, src/shell/, src/shell/Shell.cpp
    REQUIRE(items.size() == 7);
    for (auto const& item: items)
        CHECK(item.displayText.starts_with("src"));
}

TEST_CASE("FilePathCompleter.fuzzy_match_mc", "[agent][filepath][completer]")
{
    auto completer = createTestCompleter();

    // "mc" should fuzzy-match "src/main.cpp" (m-c)
    auto const items = completer->complete("@mc", 3);
    auto found = false;
    for (auto const& item: items)
    {
        if (item.displayText == "src/main.cpp")
            found = true;
    }
    CHECK(found);
}

TEST_CASE("FilePathCompleter.mid_input_trigger", "[agent][filepath][completer]")
{
    auto completer = createTestCompleter();

    // '@' after whitespace in the middle of input
    auto const items = completer->complete("review @src", 11);
    REQUIRE(!items.empty());
    for (auto const& item: items)
        CHECK(item.displayText.starts_with("src"));
}

TEST_CASE("FilePathCompleter.no_trigger_without_whitespace_before_at", "[agent][filepath][completer]")
{
    auto completer = createTestCompleter();

    // "user@src" — '@' not preceded by whitespace or at start
    auto const items = completer->complete("user@src", 8);
    CHECK(items.empty());
}

TEST_CASE("FilePathCompleter.no_trigger_without_at", "[agent][filepath][completer]")
{
    auto completer = createTestCompleter();

    CHECK(completer->complete("src/main", 8).empty());
    CHECK(completer->complete("", 0).empty());
}

TEST_CASE("FilePathCompleter.empty_file_list_returns_empty", "[agent][filepath][completer]")
{
    auto completer = FilePathCompleter {};
    CHECK(completer.complete("@", 1).empty());
}

TEST_CASE("FilePathCompleter.text_includes_at_prefix", "[agent][filepath][completer]")
{
    auto completer = createTestCompleter();

    auto const items = completer->complete("@README", 7);
    REQUIRE(items.size() == 1);
    CHECK(items[0].text == "@README.md");
    CHECK(items[0].displayText == "README.md");
}

TEST_CASE("FilePathCompleter.space_after_query_stops_trigger", "[agent][filepath][completer]")
{
    auto completer = createTestCompleter();

    // Whitespace between '@src' and cursor means we're no longer in @-context
    auto const items = completer->complete("@src more", 9);
    CHECK(items.empty());
}

TEST_CASE("FilePathCompleter.priority_is_75", "[agent][filepath][completer]")
{
    auto completer = FilePathCompleter {};
    CHECK(completer.priority() == 75);
}

TEST_CASE("FilePathCompleter.file_description_is_file", "[agent][filepath][completer]")
{
    auto completer = createTestCompleter();

    auto const items = completer->complete("@README", 7);
    REQUIRE(!items.empty());
    CHECK(items[0].description == "file");
}

TEST_CASE("FilePathCompleter.directory_description_is_directory", "[agent][filepath][completer]")
{
    auto completer = createTestCompleter();

    // "src/" is a directory entry — should have "directory" description
    auto const items = completer->complete("@src/", 5);
    auto foundDir = false;
    for (auto const& item: items)
    {
        if (item.displayText == "src/" && item.description == "directory")
            foundDir = true;
    }
    CHECK(foundDir);
}

TEST_CASE("FilePathCompleter.at_start_of_input", "[agent][filepath][completer]")
{
    auto completer = createTestCompleter();

    // '@' at position 0 is valid
    auto const items = completer->complete("@CMake", 6);
    REQUIRE(items.size() == 1);
    CHECK(items[0].text == "@CMakeLists.txt");
}

TEST_CASE("FilePathCompleter.multiple_at_uses_last", "[agent][filepath][completer]")
{
    auto completer = createTestCompleter();

    // Second '@' after whitespace — should use the last one
    auto const items = completer->complete("@README @src", 12);
    REQUIRE(!items.empty());
    for (auto const& item: items)
        CHECK(item.displayText.starts_with("src"));
}

TEST_CASE("FilePathCompleter.directories_included_in_results", "[agent][filepath][completer]")
{
    auto completer = createTestCompleter();

    // Querying "@src/a" should match src/agent/ directory and src/agent/ files
    auto const items = completer->complete("@src/a", 6);
    auto hasDir = false;
    auto hasFile = false;
    for (auto const& item: items)
    {
        if (item.displayText == "src/agent/" && item.description == "directory")
            hasDir = true;
        if (item.displayText == "src/agent/FilePathCompleter.hpp" && item.description == "file")
            hasFile = true;
    }
    CHECK(hasDir);
    CHECK(hasFile);
}
