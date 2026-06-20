// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

#include <platform/testing/InMemoryFileSystem.hpp>

using endo::platform::FileSystem;
using endo::platform::testing::InMemoryFileSystem;

namespace
{
/// Builds a small tree used by the walk tests.
InMemoryFileSystem makeTree()
{
    return InMemoryFileSystem {
        { .path = "/root", .isDirectory = true },
        { .path = "/root/a", .isDirectory = true },
        { .path = "/root/a/file1.txt", .content = "one" },
        { .path = "/root/a/file2.txt", .content = "two" },
        { .path = "/root/b", .isDirectory = true },
        { .path = "/root/b/file3.txt", .content = "three" },
    };
}
} // namespace

TEST_CASE("walkDirectoryRecursive yields every entry exactly once", "[FileSystem]")
{
    auto const fs = makeTree();

    auto visited = std::vector<std::string> {};
    for (auto const& entry: fs.walkDirectoryRecursive("/root"))
        visited.push_back(entry.path.generic_string());

    // The streaming walk must yield the same set of entries the materializing
    // listDirectoryRecursive returns — it is the single source of truth.
    auto const listed = fs.listDirectoryRecursive("/root");
    REQUIRE(listed.has_value());
    REQUIRE(visited.size() == listed->size());

    std::ranges::sort(visited);
    REQUIRE(std::ranges::find(visited, "/root/a") != visited.end());
    REQUIRE(std::ranges::find(visited, "/root/a/file1.txt") != visited.end());
    REQUIRE(std::ranges::find(visited, "/root/b/file3.txt") != visited.end());
}

TEST_CASE("walkDirectoryRecursive stops when the consumer breaks out", "[FileSystem]")
{
    auto const fs = makeTree();

    // Breaking out after the first entry is exactly how `find` aborts on Ctrl+C:
    // the lazy walk must not keep producing entries once the consumer stops.
    auto visitCount = 0;
    for ([[maybe_unused]] auto const& entry: fs.walkDirectoryRecursive("/root"))
    {
        ++visitCount;
        break;
    }

    REQUIRE(visitCount == 1);
}

TEST_CASE("walkDirectoryRecursive yields nothing for a non-directory", "[FileSystem]")
{
    auto const fs = makeTree();

    auto visitCount = 0;
    for ([[maybe_unused]] auto const& entry: fs.walkDirectoryRecursive("/does/not/exist"))
        ++visitCount;

    REQUIRE(visitCount == 0);
}
