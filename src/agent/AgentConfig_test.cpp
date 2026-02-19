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
    CHECK(config.activeProvider.empty()); // empty = auto-detect
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
// YAML loading tests (backward compatibility — loads everything)
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

    // Backward compat: activeProvider still loaded from YAML
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
// API key loading tests
// =============================================================================

TEST_CASE("agent.config.load_yaml_with_api_keys")
{
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-test-config-apikey";
    std::filesystem::create_directories(tmpDir);
    auto const configPath = tmpDir / "agent.yml";

    {
        std::ofstream f(configPath);
        f << R"(
claude:
  api_key: sk-ant-test-key-123
  model: claude-opus-4-20250514

openai:
  api_key: sk-openai-test-key-456

gemini:
  api_key: AIzaSy-test-key-789
)";
    }

    auto result = loadAgentConfig(configPath);
    REQUIRE(result.has_value());
    auto const& config = *result;

    CHECK(config.claude.apiKey == "sk-ant-test-key-123");
    CHECK(config.claude.model == "claude-opus-4-20250514");
    CHECK(config.openai.apiKey == "sk-openai-test-key-456");
    CHECK(config.gemini.apiKey == "AIzaSy-test-key-789");

    std::filesystem::remove_all(tmpDir);
}

// =============================================================================
// Config save/load round-trip tests (key-store only)
// =============================================================================

TEST_CASE("agent.config.save_only_persists_api_keys")
{
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-test-config-save";
    std::filesystem::create_directories(tmpDir);
    auto const configPath = tmpDir / "agent.yml";

    auto original = AgentConfig {};
    original.activeProvider = "openai";
    original.claude.apiKey = "sk-ant-test-roundtrip";
    original.claude.model = "claude-opus-4-20250514";
    original.openai.apiKey = "sk-openai-test-roundtrip";
    original.openai.model = "gpt-4-turbo";
    original.openai.baseUrl = "https://custom.openai.com/v1";
    original.gemini.apiKey = "AIzaSy-test-roundtrip";
    original.gemini.model = "gemini-2.5-pro";
    original.maxToolResultSize = 65536;

    auto saveError = saveAgentConfig(original, configPath);
    REQUIRE(!saveError.has_value());

    auto loadResult = loadAgentConfig(configPath);
    REQUIRE(loadResult.has_value());
    auto const& loaded = *loadResult;

    // API keys are persisted
    CHECK(loaded.claude.apiKey == "sk-ant-test-roundtrip");
    CHECK(loaded.openai.apiKey == "sk-openai-test-roundtrip");
    CHECK(loaded.gemini.apiKey == "AIzaSy-test-roundtrip");

    // Non-key fields are NOT persisted — they revert to defaults on reload
    CHECK(loaded.activeProvider.empty());                       // not saved
    CHECK(loaded.claude.model == "claude-sonnet-4-5-20250929"); // default, not "claude-opus"
    CHECK(loaded.openai.model == "gpt-4o");                     // default, not "gpt-4-turbo"
    CHECK(loaded.openai.baseUrl.empty());                       // not saved
    CHECK(loaded.gemini.model == "gemini-2.5-flash");           // default
    CHECK(loaded.maxToolResultSize == 30720);                   // default

    std::filesystem::remove_all(tmpDir);
}

TEST_CASE("agent.config.save_creates_parent_directories")
{
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-test-config-mkdir" / "nested" / "dirs";
    auto const configPath = tmpDir / "agent.yml";

    // Ensure the nested path doesn't exist
    std::filesystem::remove_all(std::filesystem::temp_directory_path() / "endo-test-config-mkdir");

    auto config = AgentConfig {};
    config.claude.apiKey = "test-key";

    auto error = saveAgentConfig(config, configPath);
    REQUIRE(!error.has_value());
    CHECK(std::filesystem::exists(configPath));

    std::filesystem::remove_all(std::filesystem::temp_directory_path() / "endo-test-config-mkdir");
}

TEST_CASE("agent.config.save_only_non_defaults")
{
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-test-config-defaults";
    std::filesystem::create_directories(tmpDir);
    auto const configPath = tmpDir / "agent.yml";

    // Save with all defaults — should produce minimal YAML
    auto config = AgentConfig {};
    auto error = saveAgentConfig(config, configPath);
    REQUIRE(!error.has_value());

    // Load it back — should still be defaults
    auto loadResult = loadAgentConfig(configPath);
    REQUIRE(loadResult.has_value());
    CHECK(loadResult->activeProvider.empty());
    CHECK(loadResult->claude.apiKey.empty());
    CHECK(loadResult->claude.model == "claude-sonnet-4-5-20250929");

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

TEST_CASE("agent.config.resolve_provider_api_key_stored_takes_priority")
{
    // Stored key should be returned even when env var is also set
    auto result = resolveProviderApiKey("stored-key-value", "HOME");
    REQUIRE(result.has_value());
    CHECK(*result == "stored-key-value");
}

TEST_CASE("agent.config.resolve_provider_api_key_env_fallback")
{
    // When stored key is empty, should fall back to env var
    auto result = resolveProviderApiKey("", "HOME");
    REQUIRE(result.has_value());
    CHECK(!result->empty());
}

TEST_CASE("agent.config.resolve_provider_api_key_neither")
{
    auto result = resolveProviderApiKey("", "ENDO_TEST_NONEXISTENT_KEY_12345");
    CHECK(!result.has_value());
}

// =============================================================================
// TraceConfig tests (loading still works for backward compat)
// =============================================================================

TEST_CASE("agent.config.trace_defaults")
{
    auto config = AgentConfig {};
    CHECK_FALSE(config.trace.enabled);
    CHECK(config.trace.defaultPath.empty());
}

TEST_CASE("agent.config.trace_yaml_loads")
{
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-test-config-trace";
    std::filesystem::create_directories(tmpDir);
    auto const configPath = tmpDir / "agent.yml";

    {
        std::ofstream f(configPath);
        f << R"(
trace:
  enabled: true
  default_path: /tmp/my-trace.jsonl
)";
    }

    auto loadResult = loadAgentConfig(configPath);
    REQUIRE(loadResult.has_value());
    CHECK(loadResult->trace.enabled == true);
    CHECK(loadResult->trace.defaultPath == "/tmp/my-trace.jsonl");

    std::filesystem::remove_all(tmpDir);
}

// =============================================================================
// ExploreConfig tests
// =============================================================================

TEST_CASE("agent.config.explore_defaults")
{
    auto config = AgentConfig {};
    CHECK(config.explore.maxTurns == 10);
}

TEST_CASE("agent.config.explore_yaml_loads")
{
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-test-config-explore";
    std::filesystem::create_directories(tmpDir);
    auto const configPath = tmpDir / "agent.yml";

    {
        std::ofstream f(configPath);
        f << R"(
explore:
  max_turns: 25
)";
    }

    auto loadResult = loadAgentConfig(configPath);
    REQUIRE(loadResult.has_value());
    CHECK(loadResult->explore.maxTurns == 25);

    std::filesystem::remove_all(tmpDir);
}
