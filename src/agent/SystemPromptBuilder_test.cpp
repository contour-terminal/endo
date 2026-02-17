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
