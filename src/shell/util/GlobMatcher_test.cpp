// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "GlobMatcher.hpp"
#include <platform/NativeFileSystem.hpp>
#include <platform/testing/InMemoryFileSystem.hpp>
#include <testing/ScopedTempDir.hpp>
#include <testing/ScopedWorkingDirectory.hpp>

using endo::expandGlobPattern;
using endo::expandRecursiveGlob;
using endo::platform::testing::InMemoryFileSystem;

// ============================================================================
// Recursive globs must be spelled the same way whichever filesystem answers
// ============================================================================

TEST_CASE("expandRecursiveGlob spells matches relative to the walk root", "[glob]")
{
    // The injected filesystem resolves "." to its own working directory and reports absolute
    // paths; a relative pattern must not pick that up, or an in-memory test would exercise
    // argument strings the real shell never produces.
    auto fs = InMemoryFileSystem {};
    fs.addFile("/test/sub/deep/a.txt", "a");
    fs.addFile("/test/sub/b.txt", "b");
    fs.addFile("/test/sub/c.log", "c");
    fs.setCurrentPath("/test");

    auto matches = expandRecursiveGlob(fs, "**/*.txt");
    std::ranges::sort(matches);

    CHECK(matches == std::vector<std::string> { "sub/b.txt", "sub/deep/a.txt" });
}

#if !defined(_WIN32)
TEST_CASE("expandRecursiveGlob agrees with the real filesystem", "[glob]")
{
    // The real walk yields "./sub/a.txt" for a root of "." -- relative, where the injected one
    // yields an absolute path. Both must arrive at the same spelling, which is the one
    // expandGlobPattern() already produces for a match in ".": no leading "./".
    auto const dir = endo::testing::ScopedTempDir { "endo_glob_shape" };
    std::filesystem::create_directories(dir / "sub" / "deep");
    {
        std::ofstream { dir / "sub" / "b.txt" } << "b";
    }
    {
        std::ofstream { dir / "sub" / "deep" / "a.txt" } << "a";
    }
    {
        std::ofstream { dir / "sub" / "c.log" } << "c";
    }

    auto const guard = endo::testing::ScopedWorkingDirectory { dir.path() };

    auto matches = expandRecursiveGlob(endo::platform::NativeFileSystem::instance(), "**/*.txt");
    std::ranges::sort(matches);

    CHECK(matches == std::vector<std::string> { "sub/b.txt", "sub/deep/a.txt" });
}
#endif

TEST_CASE("expandGlobPattern spells a match in the working directory as a bare name", "[glob]")
{
    // The shape expandRecursiveGlob() above is required to agree with.
    auto fs = InMemoryFileSystem {};
    fs.addFile("/test/a.txt", "a");
    fs.addFile("/test/b.log", "b");
    fs.setCurrentPath("/test");

    CHECK(expandGlobPattern(fs, "*.txt") == std::vector<std::string> { "a.txt" });
}
