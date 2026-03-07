// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <agent/tools/ShellExecuteTool.hpp>

using namespace endo::agent;

namespace
{

auto makeMockCallback(ShellExecResult result) -> ShellExecuteCallback
{
    return
        [result = std::move(result)](std::string const& /*command*/, std::chrono::milliseconds /*timeout*/) {
            return result;
        };
}

} // namespace

TEST_CASE("ShellExecuteTool.normal_execution", "[agent][tools]")
{
    auto tool = ShellExecuteTool(makeMockCallback(ShellExecResult {
        .output = "Hello, World!\n",
        .exitCode = 0,
        .timedOut = false,
    }));

    auto const args = nlohmann::json { { "command", "echo Hello, World!" } };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK_FALSE(result->isError);
    CHECK(result->content.find("Exit code: 0") != std::string::npos);
    CHECK(result->content.find("Hello, World!") != std::string::npos);
}

TEST_CASE("ShellExecuteTool.error_exit_code", "[agent][tools]")
{
    auto tool = ShellExecuteTool(makeMockCallback(ShellExecResult {
        .output = "error: command failed\n",
        .exitCode = 1,
        .timedOut = false,
    }));

    auto const args = nlohmann::json { { "command", "false" } };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(result->isError);
    CHECK(result->content.find("Exit code: 1") != std::string::npos);
}

TEST_CASE("ShellExecuteTool.timeout", "[agent][tools]")
{
    auto tool = ShellExecuteTool(makeMockCallback(ShellExecResult {
        .output = "partial output",
        .exitCode = -1,
        .timedOut = true,
    }));

    auto const args = nlohmann::json { { "command", "sleep 999" }, { "timeout_ms", 1000 } };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(result->isError);
    CHECK(result->content.find("timed out") != std::string::npos);
}

TEST_CASE("ShellExecuteTool.output_truncation", "[agent][tools]")
{
    // Create output larger than 30KB
    auto largeOutput = std::string(40000, 'x');

    auto tool = ShellExecuteTool(makeMockCallback(ShellExecResult {
        .output = std::move(largeOutput),
        .exitCode = 0,
        .timedOut = false,
    }));

    auto const args = nlohmann::json { { "command", "cat bigfile" } };
    auto const result = tool.execute(args);

    REQUIRE(result.has_value());
    CHECK(result->content.find("truncated") != std::string::npos);
}

TEST_CASE("ShellExecuteTool.missing_command", "[agent][tools]")
{
    auto tool = ShellExecuteTool(makeMockCallback(ShellExecResult {}));

    auto const result = tool.execute(nlohmann::json::object());

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find("command") != std::string::npos);
}

TEST_CASE("ShellExecuteTool.captures_command_and_timeout", "[agent][tools]")
{
    auto capturedCommand = std::string {};
    auto capturedTimeout = std::chrono::milliseconds {};

    auto tool = ShellExecuteTool([&](std::string const& command, std::chrono::milliseconds timeout) {
        capturedCommand = command;
        capturedTimeout = timeout;
        return ShellExecResult { .output = "ok", .exitCode = 0, .timedOut = false };
    });

    auto const args = nlohmann::json { { "command", "ls -la" }, { "timeout_ms", 5000 } };
    [[maybe_unused]] auto _ = tool.execute(args);

    CHECK(capturedCommand == "ls -la");
    CHECK(capturedTimeout == std::chrono::milliseconds(5000));
}
