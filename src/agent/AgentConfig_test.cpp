// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include <agent/AgentConfig.hpp>

using namespace endo::agent;

// =============================================================================
// Default configuration tests
// =============================================================================

TEST_CASE("agent.config.defaults")
{
    auto config = AgentConfig {};
    CHECK(config.activeProvider == "claude");
    CHECK(config.claude.apiKeyEnv == "ANTHROPIC_API_KEY");
    CHECK(config.claude.model == "claude-sonnet-4-5-20250929");
    CHECK(config.claude.maxTokens == 8192);
    CHECK(config.openai.apiKeyEnv == "OPENAI_API_KEY");
    CHECK(config.openai.model == "gpt-4o");
    CHECK(config.openai.baseUrl.empty());
    CHECK(config.openai.maxTokens == 4096);
    CHECK(config.gemini.apiKeyEnv == "GEMINI_API_KEY");
    CHECK(config.gemini.model == "gemini-2.5-flash");
    CHECK(config.gemini.maxTokens == 8192);
}

// =============================================================================
// YAML loading tests
// =============================================================================

TEST_CASE("agent.config.load_full_yaml")
{
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-test-config";
    std::filesystem::create_directories(tmpDir);
    auto const configPath = tmpDir / "agent.yml";

    {
        std::ofstream f(configPath);
        f << R"(
active_provider: gemini

claude:
  api_key_env: MY_CLAUDE_KEY
  model: claude-opus-4-20250514
  max_tokens: 16384

openai:
  api_key_env: MY_OPENAI_KEY
  model: gpt-4-turbo
  base_url: https://custom.openai.com/v1
  max_tokens: 8192

openai_compat:
  api_key_env: LOCAL_KEY
  model: llama3
  base_url: http://localhost:11434/v1
  max_tokens: 2048

gemini:
  api_key_env: MY_GEMINI_KEY
  model: gemini-2.5-pro
  max_tokens: 32768
)";
    }

    auto result = loadAgentConfig(configPath);
    REQUIRE(result.has_value());
    auto const& config = *result;

    CHECK(config.activeProvider == "gemini");

    CHECK(config.claude.apiKeyEnv == "MY_CLAUDE_KEY");
    CHECK(config.claude.model == "claude-opus-4-20250514");
    CHECK(config.claude.maxTokens == 16384);

    CHECK(config.openai.apiKeyEnv == "MY_OPENAI_KEY");
    CHECK(config.openai.model == "gpt-4-turbo");
    CHECK(config.openai.baseUrl == "https://custom.openai.com/v1");
    CHECK(config.openai.maxTokens == 8192);

    CHECK(config.openaiCompat.apiKeyEnv == "LOCAL_KEY");
    CHECK(config.openaiCompat.model == "llama3");
    CHECK(config.openaiCompat.baseUrl == "http://localhost:11434/v1");
    CHECK(config.openaiCompat.maxTokens == 2048);

    CHECK(config.gemini.apiKeyEnv == "MY_GEMINI_KEY");
    CHECK(config.gemini.model == "gemini-2.5-pro");
    CHECK(config.gemini.maxTokens == 32768);

    std::filesystem::remove_all(tmpDir);
}

TEST_CASE("agent.config.load_partial_yaml")
{
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-test-config-partial";
    std::filesystem::create_directories(tmpDir);
    auto const configPath = tmpDir / "agent.yml";

    {
        std::ofstream f(configPath);
        f << R"(
active_provider: openai

openai:
  model: gpt-4o-mini
)";
    }

    auto result = loadAgentConfig(configPath);
    REQUIRE(result.has_value());
    auto const& config = *result;

    CHECK(config.activeProvider == "openai");
    // OpenAI partially overridden
    CHECK(config.openai.model == "gpt-4o-mini");
    CHECK(config.openai.apiKeyEnv == "OPENAI_API_KEY"); // default preserved
    // Others at defaults
    CHECK(config.claude.model == "claude-sonnet-4-5-20250929");
    CHECK(config.gemini.model == "gemini-2.5-flash");

    std::filesystem::remove_all(tmpDir);
}

TEST_CASE("agent.config.load_nonexistent_file")
{
    auto result = loadAgentConfig("/tmp/endo-nonexistent-agent-config.yml");
    REQUIRE(!result.has_value());
    CHECK(!result.error().empty());
}

TEST_CASE("agent.config.load_malformed_yaml")
{
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-test-config-bad";
    std::filesystem::create_directories(tmpDir);
    auto const configPath = tmpDir / "agent.yml";

    {
        std::ofstream f(configPath);
        f << "{{{{invalid yaml content";
    }

    auto result = loadAgentConfig(configPath);
    REQUIRE(!result.has_value());
    CHECK(!result.error().empty());

    std::filesystem::remove_all(tmpDir);
}

// =============================================================================
// API key resolution tests
// =============================================================================

TEST_CASE("agent.config.resolve_api_key_missing")
{
    auto result = resolveApiKey("ENDO_TEST_NONEXISTENT_KEY_12345");
    CHECK(!result.has_value());
}

TEST_CASE("agent.config.resolve_api_key_present")
{
    // HOME is almost always set
    auto result = resolveApiKey("HOME");
    REQUIRE(result.has_value());
    CHECK(!result->empty());
}
