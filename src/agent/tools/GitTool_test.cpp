// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <agent/tools/GitTool.hpp>

using namespace endo::agent;

namespace
{

auto makeCapturingCallback(std::string& capturedCommand) -> ShellExecuteCallback
{
    return [&](std::string const& command, std::chrono::milliseconds /*timeout*/) {
        capturedCommand = command;
        return ShellExecResult { .output = "git output\n", .exitCode = 0, .timedOut = false };
    };
}

} // namespace

TEST_CASE("GitTool.read_only_command", "[agent][tools]")
{
    auto capturedCommand = std::string {};
    auto tool = GitTool(makeCapturingCallback(capturedCommand));

    auto const args = nlohmann::json { { "subcommand", "status" } };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK_FALSE(result->isError);
    CHECK(capturedCommand == "git status");
    CHECK(result->content.find("git output") != std::string::npos);
}

TEST_CASE("GitTool.command_with_args", "[agent][tools]")
{
    auto capturedCommand = std::string {};
    auto tool = GitTool(makeCapturingCallback(capturedCommand));

    auto const args = nlohmann::json {
        { "subcommand", "log" },
        { "args", nlohmann::json::array({ "--oneline", "-5" }) },
    };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(capturedCommand == "git log --oneline -5");
}

TEST_CASE("GitTool.blocked_force_push", "[agent][tools]")
{
    auto capturedCommand = std::string {};
    auto tool = GitTool(makeCapturingCallback(capturedCommand));

    auto const args = nlohmann::json {
        { "subcommand", "push" },
        { "args", nlohmann::json::array({ "--force" }) },
    };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(result->isError);
    CHECK(result->content.find("Blocked") != std::string::npos);
    CHECK(capturedCommand.empty()); // Command should not have been executed
}

TEST_CASE("GitTool.blocked_hard_reset", "[agent][tools]")
{
    auto capturedCommand = std::string {};
    auto tool = GitTool(makeCapturingCallback(capturedCommand));

    auto const args = nlohmann::json {
        { "subcommand", "reset" },
        { "args", nlohmann::json::array({ "--hard", "HEAD~1" }) },
    };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(result->isError);
    CHECK(result->content.find("Blocked") != std::string::npos);
}

TEST_CASE("GitTool.blocked_force_clean", "[agent][tools]")
{
    auto capturedCommand = std::string {};
    auto tool = GitTool(makeCapturingCallback(capturedCommand));

    auto const args = nlohmann::json {
        { "subcommand", "clean" },
        { "args", nlohmann::json::array({ "-f" }) },
    };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(result->isError);
    CHECK(result->content.find("Blocked") != std::string::npos);
}

TEST_CASE("GitTool.blocked_branch_force_delete", "[agent][tools]")
{
    auto capturedCommand = std::string {};
    auto tool = GitTool(makeCapturingCallback(capturedCommand));

    auto const args = nlohmann::json {
        { "subcommand", "branch" },
        { "args", nlohmann::json::array({ "-D", "feature-branch" }) },
    };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(result->isError);
    CHECK(result->content.find("Blocked") != std::string::npos);
}

TEST_CASE("GitTool.allowed_push_without_force", "[agent][tools]")
{
    auto capturedCommand = std::string {};
    auto tool = GitTool(makeCapturingCallback(capturedCommand));

    auto const args = nlohmann::json {
        { "subcommand", "push" },
        { "args", nlohmann::json::array({ "origin", "main" }) },
    };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK_FALSE(result->isError);
    CHECK(capturedCommand == "git push origin main");
}

TEST_CASE("GitTool.missing_subcommand", "[agent][tools]")
{
    auto capturedCommand = std::string {};
    auto tool = GitTool(makeCapturingCallback(capturedCommand));

    auto const result = tool.execute(nlohmann::json::object());

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("subcommand") != std::string::npos);
}
