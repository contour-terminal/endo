// SPDX-License-Identifier: Apache-2.0
#include <core/Environment.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include <platform/CaptureOutput.hpp>
#include <platform/ProjectFileTree.hpp>

using namespace endo::platform;

namespace
{
/// Creates a temporary directory with a known structure for testing.
struct TempProjectDir
{
    std::filesystem::path root;

    TempProjectDir()
    {
        root = std::filesystem::temp_directory_path() / "endo-test-filetree";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root / "src" / "agent");
        std::filesystem::create_directories(root / "src" / "shell");
        std::filesystem::create_directories(root / "docs");

        // Create some files
        std::ofstream(root / "README.md") << "readme";
        std::ofstream(root / "CMakeLists.txt") << "cmake";
        std::ofstream(root / "src" / "main.cpp") << "main";
        std::ofstream(root / "src" / "agent" / "Agent.hpp") << "agent";
        std::ofstream(root / "src" / "agent" / "Agent.cpp") << "agent";
        std::ofstream(root / "src" / "shell" / "Shell.cpp") << "shell";
        std::ofstream(root / "docs" / "guide.md") << "guide";
    }

    ~TempProjectDir() { std::filesystem::remove_all(root); }
};
} // namespace

TEST_CASE("ProjectFileTree.generates_from_directory", "[platform]")
{
    auto dir = TempProjectDir {};
    auto config = FileTreeConfig { .respectGitignore = false };
    auto tree = ProjectFileTree(config);

    auto const result = tree.generate(dir.root);
    CHECK(!result.empty());
    CHECK(result.find("src/") != std::string::npos);
    CHECK(result.find("README.md") != std::string::npos);
}

TEST_CASE("ProjectFileTree.respects_depth_limit", "[platform]")
{
    auto dir = TempProjectDir {};
    auto config = FileTreeConfig { .maxDepth = 1, .respectGitignore = false };
    auto tree = ProjectFileTree(config);

    auto const result = tree.generate(dir.root);
    CHECK(!result.empty());
    // Should include top-level files
    CHECK(result.find("README.md") != std::string::npos);
    // Should include first-level files (depth 1)
    CHECK(result.find("main.cpp") != std::string::npos);
    // Should NOT include files deeper than maxDepth + 1 levels
    CHECK(result.find("Agent.hpp") == std::string::npos);
}

TEST_CASE("ProjectFileTree.respects_entry_limit", "[platform]")
{
    auto dir = TempProjectDir {};
    auto config = FileTreeConfig { .maxEntries = 3, .respectGitignore = false };
    auto tree = ProjectFileTree(config);

    auto const result = tree.generate(dir.root);
    CHECK(!result.empty());
    // With only 3 entries, we should have a truncated tree
    auto lineCount = size_t { 0 };
    for (auto ch: result)
    {
        if (ch == '\n')
            ++lineCount;
    }
    CHECK(lineCount <= 5); // Allow for directories that contain the entries

    // Truncation is deterministic: entries are sorted before the limit is
    // applied, so the lexicographically-first paths survive regardless of the
    // filesystem iteration order. The first three sorted candidates are
    // CMakeLists.txt, README.md and docs/guide.md.
    CHECK(result.find("CMakeLists.txt") != std::string::npos);
    CHECK(result.find("README.md") != std::string::npos);
    CHECK(result.find("docs/") != std::string::npos);
    CHECK(result.find("guide.md") != std::string::npos);
    // Entries that sort after the cutoff must be excluded.
    CHECK(result.find("main.cpp") == std::string::npos);
    CHECK(result.find("Shell.cpp") == std::string::npos);
    CHECK(result.find("Agent.") == std::string::npos);
}

TEST_CASE("ProjectFileTree.empty_directory", "[platform]")
{
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-test-filetree-empty";
    std::filesystem::remove_all(tmpDir);
    std::filesystem::create_directories(tmpDir);

    auto config = FileTreeConfig { .respectGitignore = false };
    auto tree = ProjectFileTree(config);

    auto const result = tree.generate(tmpDir);
    CHECK(result.empty());

    std::filesystem::remove_all(tmpDir);
}

TEST_CASE("ProjectFileTree.filePaths_includes_files_and_directories", "[platform]")
{
    auto dir = TempProjectDir {};
    auto config = FileTreeConfig { .respectGitignore = false };
    auto tree = ProjectFileTree(config);

    auto const paths = tree.filePaths(dir.root);
    CHECK(!paths.empty());

    // Should contain file entries
    CHECK(std::ranges::find(paths, "README.md") != paths.end());
    CHECK(std::ranges::find(paths, "src/main.cpp") != paths.end());

    // Should contain directory entries (trailing '/')
    CHECK(std::ranges::find(paths, "src/") != paths.end());
    CHECK(std::ranges::find(paths, "src/agent/") != paths.end());
    CHECK(std::ranges::find(paths, "docs/") != paths.end());
}

TEST_CASE("ProjectFileTree.tree_format_verification", "[platform]")
{
    auto dir = TempProjectDir {};
    auto config = FileTreeConfig { .respectGitignore = false };
    auto tree = ProjectFileTree(config);

    auto const result = tree.generate(dir.root);
    // Directories should end with /
    CHECK(result.find("src/") != std::string::npos);
    // Files should not end with /
    auto const readmePos = result.find("README.md");
    CHECK(readmePos != std::string::npos);
    if (readmePos != std::string::npos)
    {
        auto const afterReadme = readmePos + std::string("README.md").size();
        CHECK(result[afterReadme] == '\n');
    }
}

TEST_CASE("ProjectFileTree.lists_a_repository_through_git_whatever_its_directory_is_called", "[platform]")
{
    // The directory is an argument to git, never text a shell parses: a space or `&` in its name used
    // to split the command, or on Windows run what followed. There the command also ended in a
    // `2>/dev/null` that cmd.exe cannot open, so it failed before git ran and the tree came from the
    // directory walk instead. The ignored file tells the two apart: only git leaves it out. It sits
    // below the root because every root-level file is listed either way, as a navigation target.
    struct Repository
    {
        std::filesystem::path root = std::filesystem::temp_directory_path() / "endo-test-filetree git & repo";

        Repository()
        {
            std::filesystem::remove_all(root);
            std::filesystem::create_directories(root / "src");
            std::ofstream(root / ".gitignore") << "ignored.txt\n";
            std::ofstream(root / "src" / "kept.txt") << "kept";
            std::ofstream(root / "src" / "ignored.txt") << "ignored";
        }

        ~Repository()
        {
            auto ignored = std::error_code {};
            std::filesystem::remove_all(root, ignored);
        }

        Repository(Repository const&) = delete;
        Repository& operator=(Repository const&) = delete;
        Repository(Repository&&) = delete;
        Repository& operator=(Repository&&) = delete;
    };

    auto const repository = Repository {};
    auto const init = captureOutput(
        "git", std::vector<std::string> { "init", "-q", repository.root.string() }, core::LiveEnvironment {});
    if (!init || init->exitCode != 0)
        SKIP("git is not available to make a repository with");

    auto const paths = ProjectFileTree {}.filePaths(repository.root);

    CHECK(std::ranges::find(paths, "src/kept.txt") != paths.end());
    CHECK(std::ranges::find(paths, "src/ignored.txt") == paths.end());
}
