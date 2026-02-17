// SPDX-License-Identifier: Apache-2.0
#include <http/HttpClient.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>

#include <agent/AgentConfig.hpp>
#include <agent/ProviderFactory.hpp>

using namespace endo::agent;

// =============================================================================
// ProviderFactory tests
// =============================================================================

TEST_CASE("agent.factory.no_keys_no_providers")
{
    // Ensure no relevant API keys are set
    ::unsetenv("ENDO_TEST_CLAUDE_KEY");
    ::unsetenv("ENDO_TEST_OPENAI_KEY");
    ::unsetenv("ENDO_TEST_GEMINI_KEY");

    auto config = AgentConfig {};
    config.claude.apiKeyEnv = "ENDO_TEST_CLAUDE_KEY";
    config.openai.apiKeyEnv = "ENDO_TEST_OPENAI_KEY";
    config.gemini.apiKeyEnv = "ENDO_TEST_GEMINI_KEY";
    config.openaiCompat.baseUrl = ""; // no compat provider

    endo::http::HttpClient httpClient;
    auto factory = ProviderFactory(httpClient, config);

    CHECK(factory.activeProvider() == nullptr);
    CHECK(factory.authenticatedProviders().empty());
}

TEST_CASE("agent.factory.single_provider")
{
    ::setenv("ENDO_TEST_FACTORY_KEY", "test-key-value", 1);

    auto config = AgentConfig {};
    config.activeProvider = "claude";
    config.claude.apiKeyEnv = "ENDO_TEST_FACTORY_KEY";
    config.openai.apiKeyEnv = "ENDO_TEST_NONEXISTENT_1";
    config.gemini.apiKeyEnv = "ENDO_TEST_NONEXISTENT_2";
    config.openaiCompat.baseUrl = "";

    endo::http::HttpClient httpClient;
    auto factory = ProviderFactory(httpClient, config);

    CHECK(factory.activeProvider() != nullptr);
    CHECK(factory.activeProviderName() == "claude");
    CHECK(factory.authenticatedProviders().size() == 1);

    ::unsetenv("ENDO_TEST_FACTORY_KEY");
}

TEST_CASE("agent.factory.switch_provider")
{
    ::setenv("ENDO_TEST_SWITCH_CLAUDE", "key1", 1);
    ::setenv("ENDO_TEST_SWITCH_GEMINI", "key2", 1);

    auto config = AgentConfig {};
    config.activeProvider = "claude";
    config.claude.apiKeyEnv = "ENDO_TEST_SWITCH_CLAUDE";
    config.openai.apiKeyEnv = "ENDO_TEST_NONEXISTENT_3";
    config.gemini.apiKeyEnv = "ENDO_TEST_SWITCH_GEMINI";
    config.openaiCompat.baseUrl = "";

    endo::http::HttpClient httpClient;
    auto factory = ProviderFactory(httpClient, config);

    CHECK(factory.activeProviderName() == "claude");
    CHECK(factory.authenticatedProviders().size() == 2);

    // Switch to gemini
    CHECK(factory.switchProvider("gemini"));
    CHECK(factory.activeProviderName() == "gemini");
    CHECK(factory.activeProvider() != nullptr);

    // Switch to non-existent
    CHECK(!factory.switchProvider("nonexistent"));
    CHECK(factory.activeProviderName() == "gemini"); // unchanged

    // Switch back
    CHECK(factory.switchProvider("claude"));
    CHECK(factory.activeProviderName() == "claude");

    ::unsetenv("ENDO_TEST_SWITCH_CLAUDE");
    ::unsetenv("ENDO_TEST_SWITCH_GEMINI");
}

TEST_CASE("agent.factory.fallback_when_active_not_available")
{
    ::setenv("ENDO_TEST_FALLBACK_KEY", "test-key", 1);

    auto config = AgentConfig {};
    config.activeProvider = "claude"; // Claude key not available
    config.claude.apiKeyEnv = "ENDO_TEST_NONEXISTENT_4";
    config.openai.apiKeyEnv = "ENDO_TEST_FALLBACK_KEY";
    config.gemini.apiKeyEnv = "ENDO_TEST_NONEXISTENT_5";
    config.openaiCompat.baseUrl = "";

    endo::http::HttpClient httpClient;
    auto factory = ProviderFactory(httpClient, config);

    // Should fall back to the only authenticated provider
    CHECK(factory.activeProvider() != nullptr);
    CHECK(factory.activeProviderName() == "openai");

    ::unsetenv("ENDO_TEST_FALLBACK_KEY");
}

TEST_CASE("agent.factory.openai_compat_no_key_required")
{
    ::unsetenv("ENDO_TEST_COMPAT_KEY");

    auto config = AgentConfig {};
    config.activeProvider = "openai_compat";
    config.claude.apiKeyEnv = "ENDO_TEST_NONEXISTENT_6";
    config.openai.apiKeyEnv = "ENDO_TEST_NONEXISTENT_7";
    config.gemini.apiKeyEnv = "ENDO_TEST_NONEXISTENT_8";
    config.openaiCompat.baseUrl = "http://localhost:11434/v1";
    config.openaiCompat.apiKeyEnv = "ENDO_TEST_COMPAT_KEY";
    config.openaiCompat.model = "llama3";

    endo::http::HttpClient httpClient;
    auto factory = ProviderFactory(httpClient, config);

    CHECK(factory.activeProvider() != nullptr);
    CHECK(factory.activeProviderName() == "openai_compat");
}
