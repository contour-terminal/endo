// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <agent/HeadlessRunner.hpp>

using namespace endo::agent;

// =============================================================================
// JSON serialization tests
// =============================================================================

TEST_CASE("agent.headless.toJson.success")
{
    auto result = HeadlessRunResult {
        .success = true,
        .response = "The answer is 42.",
        .tokenUsage = { .inputTokens = 100,
                        .outputTokens = 50,
                        .cacheReadTokens = 30,
                        .cacheCreationTokens = 10 },
        .turnCount = 1,
        .providerName = "claude",
        .modelName = "claude-sonnet-4-6",
    };

    auto const json = toJson(result);
    CHECK(json["success"] == true);
    CHECK(json["response"] == "The answer is 42.");
    CHECK(json["turn_count"] == 1);
    CHECK(json["provider"] == "claude");
    CHECK(json["model"] == "claude-sonnet-4-6");
    CHECK(json["tool_calls"].is_array());
    CHECK(json["tool_calls"].empty());
    CHECK(json.find("error") == json.end());

    auto const& usage = json["token_usage"];
    CHECK(usage["input_tokens"] == 100);
    CHECK(usage["output_tokens"] == 50);
    CHECK(usage["cache_read_tokens"] == 30);
    CHECK(usage["cache_write_tokens"] == 10);
}

TEST_CASE("agent.headless.toJson.failure")
{
    auto result = HeadlessRunResult {
        .success = false,
        .errorMessage = "Provider error: rate limited",
        .providerName = "openai",
        .modelName = "gpt-4o",
    };

    auto const json = toJson(result);
    CHECK(json["success"] == false);
    CHECK(json["error"] == "Provider error: rate limited");
    CHECK(json["response"] == "");
}

TEST_CASE("agent.headless.toJson.tool_calls")
{
    auto result = HeadlessRunResult {
        .success = true,
        .response = "Done.",
        .toolCalls = {
            {
                .name = "read_file",
                .arguments = { { "path", "/tmp/test.txt" } },
                .result = "file contents",
                .isError = false,
                .duration = std::chrono::milliseconds(42),
            },
            {
                .name = "shell_execute",
                .arguments = { { "command", "ls" } },
                .result = "Error: permission denied",
                .isError = true,
                .duration = std::chrono::milliseconds(100),
            },
        },
        .turnCount = 2,
        .providerName = "claude",
        .modelName = "claude-sonnet-4-6",
    };

    auto const json = toJson(result);
    REQUIRE(json["tool_calls"].size() == 2);

    auto const& tc0 = json["tool_calls"][0];
    CHECK(tc0["name"] == "read_file");
    CHECK(tc0["arguments"]["path"] == "/tmp/test.txt");
    CHECK(tc0["result"] == "file contents");
    CHECK(tc0["is_error"] == false);
    CHECK(tc0["duration_ms"] == 42);

    auto const& tc1 = json["tool_calls"][1];
    CHECK(tc1["name"] == "shell_execute");
    CHECK(tc1["is_error"] == true);
    CHECK(tc1["duration_ms"] == 100);
}

TEST_CASE("agent.headless.toJson.default_values")
{
    auto result = HeadlessRunResult {};
    auto const json = toJson(result);
    CHECK(json["success"] == false);
    CHECK(json["response"] == "");
    CHECK(json["turn_count"] == 0);
    CHECK(json["tool_calls"].empty());
}
