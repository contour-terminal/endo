// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <agent/SystemPromptBuilder.hpp>

using namespace endo::agent;

TEST_CASE("SystemPromptBuilder.default_build", "[agent]")
{
    auto builder = SystemPromptBuilder {};
    auto const prompt = builder.build();

    CHECK(prompt.find("AI assistant") != std::string::npos);
    CHECK(prompt.find("## Environment") != std::string::npos);
}

TEST_CASE("SystemPromptBuilder.with_working_directory", "[agent]")
{
    auto builder = SystemPromptBuilder {};
    builder.setWorkingDirectory("/home/user/project");

    auto const prompt = builder.build();
    CHECK(prompt.find("/home/user/project") != std::string::npos);
}

TEST_CASE("SystemPromptBuilder.with_git_info", "[agent]")
{
    auto builder = SystemPromptBuilder {};
    builder.setGitBranch("feature/new-ui");
    builder.setGitStatus("3 modified");

    auto const prompt = builder.build();
    CHECK(prompt.find("feature/new-ui") != std::string::npos);
    CHECK(prompt.find("3 modified") != std::string::npos);
}

TEST_CASE("SystemPromptBuilder.git_status_omitted_without_branch", "[agent]")
{
    auto builder = SystemPromptBuilder {};
    builder.setGitStatus("clean");

    auto const prompt = builder.build();
    // Git status should not appear without a branch
    CHECK(prompt.find("Git status") == std::string::npos);
}

TEST_CASE("SystemPromptBuilder.with_shell_info", "[agent]")
{
    auto builder = SystemPromptBuilder {};
    builder.setShellInfo("endo 0.1");

    auto const prompt = builder.build();
    CHECK(prompt.find("endo 0.1") != std::string::npos);
}

TEST_CASE("SystemPromptBuilder.full_context", "[agent]")
{
    auto builder = SystemPromptBuilder {};
    builder.setWorkingDirectory("/home/user/project");
    builder.setGitBranch("main");
    builder.setGitStatus("clean");
    builder.setShellInfo("endo 0.1");

    auto const prompt = builder.build();
    CHECK(prompt.find("/home/user/project") != std::string::npos);
    CHECK(prompt.find("main") != std::string::npos);
    CHECK(prompt.find("clean") != std::string::npos);
    CHECK(prompt.find("endo 0.1") != std::string::npos);
}

// ============================================================================
// New section tests
// ============================================================================

TEST_CASE("SystemPromptBuilder.custom_base_instructions", "[agent]")
{
    auto builder = SystemPromptBuilder {};
    builder.setBaseInstructions("You are a code reviewer.");

    auto const prompt = builder.build();
    CHECK(prompt.find("You are a code reviewer.") != std::string::npos);
    // Default instructions should NOT appear
    CHECK(prompt.find("AI assistant") == std::string::npos);
}

TEST_CASE("SystemPromptBuilder.with_global_rules", "[agent]")
{
    auto builder = SystemPromptBuilder {};
    builder.setGlobalRules({ "Always use C++23.", "Prefer ranges over raw loops." });

    auto const prompt = builder.build();
    CHECK(prompt.find("## Global Rules") != std::string::npos);
    CHECK(prompt.find("Always use C++23.") != std::string::npos);
    CHECK(prompt.find("Prefer ranges over raw loops.") != std::string::npos);
    CHECK(prompt.find("---") != std::string::npos); // Separator between rules
}

TEST_CASE("SystemPromptBuilder.with_project_rules", "[agent]")
{
    auto builder = SystemPromptBuilder {};
    builder.setProjectRules({ "Use CMake with preset clang-debug." });

    auto const prompt = builder.build();
    CHECK(prompt.find("## Project Rules") != std::string::npos);
    CHECK(prompt.find("Use CMake with preset clang-debug.") != std::string::npos);
}

TEST_CASE("SystemPromptBuilder.with_file_tree", "[agent]")
{
    auto builder = SystemPromptBuilder {};
    builder.setFileTree("src/\n  main.cpp\n  agent/\n    Agent.hpp\n");

    auto const prompt = builder.build();
    CHECK(prompt.find("## Project Structure") != std::string::npos);
    CHECK(prompt.find("src/") != std::string::npos);
    CHECK(prompt.find("main.cpp") != std::string::npos);
}

TEST_CASE("SystemPromptBuilder.with_memory_files", "[agent]")
{
    auto builder = SystemPromptBuilder {};
    builder.setMemoryFiles({ "### patterns.md\nUse RAII for resource management." });

    auto const prompt = builder.build();
    CHECK(prompt.find("## Agent Memory") != std::string::npos);
    CHECK(prompt.find("patterns.md") != std::string::npos);
    CHECK(prompt.find("Use RAII for resource management.") != std::string::npos);
}

TEST_CASE("SystemPromptBuilder.empty_sections_omitted", "[agent]")
{
    auto builder = SystemPromptBuilder {};
    // Only set environment fields, leave new sections empty

    auto const prompt = builder.build();
    CHECK(prompt.find("## Global Rules") == std::string::npos);
    CHECK(prompt.find("## Project Rules") == std::string::npos);
    CHECK(prompt.find("## Project Structure") == std::string::npos);
    CHECK(prompt.find("## Agent Memory") == std::string::npos);
    // Environment section should still be present
    CHECK(prompt.find("## Environment") != std::string::npos);
}

TEST_CASE("SystemPromptBuilder.full_context_with_all_sections", "[agent]")
{
    auto builder = SystemPromptBuilder {};
    builder.setBaseInstructions("Custom base instructions.");
    builder.setGlobalRules({ "Global rule 1." });
    builder.setProjectRules({ "Project rule 1." });
    builder.setWorkingDirectory("/home/user/project");
    builder.setGitBranch("main");
    builder.setGitStatus("clean");
    builder.setShellInfo("endo 0.1");
    builder.setFileTree("src/\n  main.cpp\n");
    builder.setMemoryFiles({ "### memory.md\nKey insight." });

    auto const prompt = builder.build();

    // Verify section ordering
    auto const basePos = prompt.find("Custom base instructions.");
    auto const globalPos = prompt.find("## Global Rules");
    auto const projectPos = prompt.find("## Project Rules");
    auto const envPos = prompt.find("## Environment");
    auto const treePos = prompt.find("## Project Structure");
    auto const memoryPos = prompt.find("## Agent Memory");

    CHECK(basePos < globalPos);
    CHECK(globalPos < projectPos);
    CHECK(projectPos < envPos);
    CHECK(envPos < treePos);
    CHECK(treePos < memoryPos);
}
