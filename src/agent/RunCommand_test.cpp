// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include <agent/RunCommand.hpp>

using namespace endo::agent;

// =============================================================================
// Basic prompt parsing
// =============================================================================

TEST_CASE("agent.run.positional_prompt")
{
    char const* args[] = { "Hello world" };
    auto result = parseAgentRunArgs(args);
    REQUIRE(result.has_value());
    CHECK(result->prompt == "Hello world");
    CHECK_FALSE(result->jsonOutput);
    CHECK(result->maxTurns == 25);
    CHECK_FALSE(result->autoApprove);
    CHECK_FALSE(result->provider.has_value());
    CHECK_FALSE(result->model.has_value());
}

TEST_CASE("agent.run.multi_word_prompt")
{
    char const* args[] = { "List", "TODO", "comments" };
    auto result = parseAgentRunArgs(args);
    REQUIRE(result.has_value());
    CHECK(result->prompt == "List TODO comments");
}

TEST_CASE("agent.run.no_prompt_error")
{
    auto result = parseAgentRunArgs({});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().find("No prompt") != std::string::npos);
}

// =============================================================================
// Flag parsing
// =============================================================================

TEST_CASE("agent.run.json_flag")
{
    char const* args[] = { "--json", "test prompt" };
    auto result = parseAgentRunArgs(args);
    REQUIRE(result.has_value());
    CHECK(result->jsonOutput);
    CHECK(result->prompt == "test prompt");
}

TEST_CASE("agent.run.auto_approve_flag")
{
    char const* args[] = { "--auto-approve", "test" };
    auto result = parseAgentRunArgs(args);
    REQUIRE(result.has_value());
    CHECK(result->autoApprove);
}

TEST_CASE("agent.run.max_turns")
{
    char const* args[] = { "--max-turns", "10", "test" };
    auto result = parseAgentRunArgs(args);
    REQUIRE(result.has_value());
    CHECK(result->maxTurns == 10);
}

TEST_CASE("agent.run.max_turns_missing_value")
{
    char const* args[] = { "--max-turns" };
    auto result = parseAgentRunArgs(args);
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("agent.run.max_turns_invalid_value")
{
    char const* args[] = { "--max-turns", "abc", "test" };
    auto result = parseAgentRunArgs(args);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().find("invalid number") != std::string::npos);
}

TEST_CASE("agent.run.provider_override")
{
    char const* args[] = { "--provider", "openai", "test" };
    auto result = parseAgentRunArgs(args);
    REQUIRE(result.has_value());
    REQUIRE(result->provider.has_value());
    CHECK(*result->provider == "openai");
}

TEST_CASE("agent.run.model_override")
{
    char const* args[] = { "--model", "gpt-4o", "test" };
    auto result = parseAgentRunArgs(args);
    REQUIRE(result.has_value());
    REQUIRE(result->model.has_value());
    CHECK(*result->model == "gpt-4o");
}

TEST_CASE("agent.run.unknown_option")
{
    char const* args[] = { "--unknown", "test" };
    auto result = parseAgentRunArgs(args);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().find("Unknown option") != std::string::npos);
}

// =============================================================================
// File prompt
// =============================================================================

TEST_CASE("agent.run.file_prompt")
{
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-test-run-cmd";
    std::filesystem::create_directories(tmpDir);
    auto const filePath = tmpDir / "prompt.txt";
    {
        auto ofs = std::ofstream(filePath);
        ofs << "Prompt from file";
    }

    char const* args[] = { "--file", filePath.c_str() };
    auto result = parseAgentRunArgs(args);
    REQUIRE(result.has_value());
    CHECK(result->prompt == "Prompt from file");

    std::filesystem::remove_all(tmpDir);
}

TEST_CASE("agent.run.file_short_flag")
{
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-test-run-cmd-f";
    std::filesystem::create_directories(tmpDir);
    auto const filePath = tmpDir / "prompt.txt";
    {
        auto ofs = std::ofstream(filePath);
        ofs << "Short flag prompt";
    }

    char const* args[] = { "-f", filePath.c_str() };
    auto result = parseAgentRunArgs(args);
    REQUIRE(result.has_value());
    CHECK(result->prompt == "Short flag prompt");

    std::filesystem::remove_all(tmpDir);
}

TEST_CASE("agent.run.file_not_found")
{
    char const* args[] = { "--file", "/nonexistent/path/file.txt" };
    auto result = parseAgentRunArgs(args);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().find("File not found") != std::string::npos);
}

// =============================================================================
// Combined flags
// =============================================================================

TEST_CASE("agent.run.all_flags")
{
    char const* args[] = {
        "--json", "--auto-approve", "--max-turns",     "5",    "--provider",
        "claude", "--model",        "claude-opus-4-6", "test",
    };
    auto result = parseAgentRunArgs(args);
    REQUIRE(result.has_value());
    CHECK(result->jsonOutput);
    CHECK(result->autoApprove);
    CHECK(result->maxTurns == 5);
    REQUIRE(result->provider.has_value());
    CHECK(*result->provider == "claude");
    REQUIRE(result->model.has_value());
    CHECK(*result->model == "claude-opus-4-6");
    CHECK(result->prompt == "test");
}
