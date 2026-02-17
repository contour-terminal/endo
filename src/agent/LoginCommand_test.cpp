// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include <agent/AgentConfig.hpp>

using namespace endo::agent;

// =============================================================================
// Config save/load integration tests for login workflow
// =============================================================================

TEST_CASE("agent.login.save_api_key_and_reload")
{
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-test-login-save";
    std::filesystem::create_directories(tmpDir);
    auto const configPath = tmpDir / "agent.yml";

    // Simulate login: set API key and active provider
    auto config = AgentConfig {};
    config.claude.apiKey = "sk-ant-api0xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
    config.activeProvider = "claude";

    auto error = saveAgentConfig(config, configPath);
    REQUIRE(!error.has_value());

    // Reload and verify
    auto loaded = loadAgentConfig(configPath);
    REQUIRE(loaded.has_value());
    CHECK(loaded->claude.apiKey == "sk-ant-api0xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
    CHECK(loaded->activeProvider == "claude");

    std::filesystem::remove_all(tmpDir);
}

TEST_CASE("agent.login.logout_clears_api_key")
{
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-test-login-logout";
    std::filesystem::create_directories(tmpDir);
    auto const configPath = tmpDir / "agent.yml";

    // Save config with API key
    auto config = AgentConfig {};
    config.claude.apiKey = "sk-ant-to-be-removed";
    auto error = saveAgentConfig(config, configPath);
    REQUIRE(!error.has_value());

    // Simulate logout: clear the key
    config.claude.apiKey.clear();
    error = saveAgentConfig(config, configPath);
    REQUIRE(!error.has_value());

    // Reload and verify key is gone
    auto loaded = loadAgentConfig(configPath);
    REQUIRE(loaded.has_value());
    CHECK(loaded->claude.apiKey.empty());

    std::filesystem::remove_all(tmpDir);
}

TEST_CASE("agent.login.switch_active_provider")
{
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-test-login-switch";
    std::filesystem::create_directories(tmpDir);
    auto const configPath = tmpDir / "agent.yml";

    auto config = AgentConfig {};
    config.claude.apiKey = "sk-ant-test";
    config.openai.apiKey = "sk-openai-test";
    config.activeProvider = "claude";

    auto error = saveAgentConfig(config, configPath);
    REQUIRE(!error.has_value());

    // Switch to openai
    config.activeProvider = "openai";
    error = saveAgentConfig(config, configPath);
    REQUIRE(!error.has_value());

    auto loaded = loadAgentConfig(configPath);
    REQUIRE(loaded.has_value());
    CHECK(loaded->activeProvider == "openai");
    // Both keys should still be present
    CHECK(loaded->claude.apiKey == "sk-ant-test");
    CHECK(loaded->openai.apiKey == "sk-openai-test");

    std::filesystem::remove_all(tmpDir);
}

TEST_CASE("agent.login.multiple_providers_saved")
{
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-test-login-multi";
    std::filesystem::create_directories(tmpDir);
    auto const configPath = tmpDir / "agent.yml";

    auto config = AgentConfig {};
    config.claude.apiKey = "sk-ant-multi";
    config.openai.apiKey = "sk-openai-multi";
    config.gemini.apiKey = "AIzaSy-multi";
    config.activeProvider = "gemini";

    auto error = saveAgentConfig(config, configPath);
    REQUIRE(!error.has_value());

    auto loaded = loadAgentConfig(configPath);
    REQUIRE(loaded.has_value());
    CHECK(loaded->activeProvider == "gemini");
    CHECK(loaded->claude.apiKey == "sk-ant-multi");
    CHECK(loaded->openai.apiKey == "sk-openai-multi");
    CHECK(loaded->gemini.apiKey == "AIzaSy-multi");

    std::filesystem::remove_all(tmpDir);
}

TEST_CASE("agent.login.atomic_write_no_corruption")
{
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-test-login-atomic";
    std::filesystem::create_directories(tmpDir);
    auto const configPath = tmpDir / "agent.yml";

    // Save initial config
    auto config = AgentConfig {};
    config.claude.apiKey = "initial-key";
    auto error = saveAgentConfig(config, configPath);
    REQUIRE(!error.has_value());

    // Overwrite with new config
    config.claude.apiKey = "updated-key";
    error = saveAgentConfig(config, configPath);
    REQUIRE(!error.has_value());

    // Verify no .tmp file left behind
    auto const tmpFile = std::filesystem::path(configPath.string() + ".tmp");
    CHECK(!std::filesystem::exists(tmpFile));

    // Verify the file has the updated content
    auto loaded = loadAgentConfig(configPath);
    REQUIRE(loaded.has_value());
    CHECK(loaded->claude.apiKey == "updated-key");

    std::filesystem::remove_all(tmpDir);
}

TEST_CASE("agent.login.openai_compat_api_key")
{
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-test-login-compat";
    std::filesystem::create_directories(tmpDir);
    auto const configPath = tmpDir / "agent.yml";

    auto config = AgentConfig {};
    config.openaiCompat.apiKey = "sk-compat-key";
    config.openaiCompat.baseUrl = "http://localhost:11434/v1";
    config.openaiCompat.model = "llama3";

    auto error = saveAgentConfig(config, configPath);
    REQUIRE(!error.has_value());

    auto loaded = loadAgentConfig(configPath);
    REQUIRE(loaded.has_value());
    CHECK(loaded->openaiCompat.apiKey == "sk-compat-key");
    CHECK(loaded->openaiCompat.baseUrl == "http://localhost:11434/v1");
    CHECK(loaded->openaiCompat.model == "llama3");

    std::filesystem::remove_all(tmpDir);
}
