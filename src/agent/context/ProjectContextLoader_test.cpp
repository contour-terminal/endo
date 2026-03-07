// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include <agent/context/ProjectContextLoader.hpp>

using namespace endo::agent;

namespace
{
struct TempProject
{
    std::filesystem::path root;

    TempProject()
    {
        root = std::filesystem::temp_directory_path() / "endo-test-context-loader";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
    }

    ~TempProject() { std::filesystem::remove_all(root); }

    void createFile(std::filesystem::path const& relPath, std::string const& content) const
    {
        auto const fullPath = root / relPath;
        std::filesystem::create_directories(fullPath.parent_path());
        std::ofstream(fullPath) << content;
    }
};
} // namespace

TEST_CASE("ProjectContextLoader.loads_claude_md", "[agent]")
{
    auto project = TempProject {};
    project.createFile("CLAUDE.md", "# Project Rules\nAlways use C++23.");
    project.createFile("src/main.cpp", "int main() {}");

    auto const ctx = ProjectContextLoader::load(project.root);

    REQUIRE(ctx.rulesFiles.size() == 1);
    CHECK(ctx.rulesFiles[0].find("CLAUDE.md") != std::string::npos);
    CHECK(ctx.rulesFiles[0].find("Always use C++23") != std::string::npos);
}

TEST_CASE("ProjectContextLoader.loads_multiple_rules_files", "[agent]")
{
    auto project = TempProject {};
    project.createFile("CLAUDE.md", "Claude rules content");
    project.createFile("AGENT.md", "Agent rules content");

    auto const ctx = ProjectContextLoader::load(project.root);

    CHECK(ctx.rulesFiles.size() == 2);
}

TEST_CASE("ProjectContextLoader.loads_endo_agent_rules", "[agent]")
{
    auto project = TempProject {};
    project.createFile(".endo/agent-rules.md", "Custom endo rules");

    auto const ctx = ProjectContextLoader::load(project.root);

    REQUIRE(ctx.rulesFiles.size() == 1);
    CHECK(ctx.rulesFiles[0].find("Custom endo rules") != std::string::npos);
}

TEST_CASE("ProjectContextLoader.no_rules_returns_empty", "[agent]")
{
    auto project = TempProject {};
    project.createFile("src/main.cpp", "int main() {}");

    auto const ctx = ProjectContextLoader::load(project.root);

    CHECK(ctx.rulesFiles.empty());
}

TEST_CASE("ProjectContextLoader.generates_file_tree", "[agent]")
{
    auto project = TempProject {};
    project.createFile("src/main.cpp", "int main() {}");
    project.createFile("README.md", "readme");

    auto const ctx = ProjectContextLoader::load(project.root);

    CHECK(!ctx.fileTree.empty());
    CHECK(ctx.fileTree.find("src/") != std::string::npos);
}

TEST_CASE("ProjectContextLoader.empty_project", "[agent]")
{
    auto project = TempProject {};

    auto const ctx = ProjectContextLoader::load(project.root);

    CHECK(ctx.rulesFiles.empty());
    CHECK(ctx.globalRules.empty()); // May or may not be empty depending on user's machine
    CHECK(ctx.fileTree.empty());
}
