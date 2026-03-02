// SPDX-License-Identifier: Apache-2.0
#include <http/HttpClient.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>

#include <agent/AgentConfig.hpp>
#include <agent/providers/ProviderFactory.hpp>
#include <testing/EnvHelper.hpp>

using namespace endo::agent;
using endo::testing::setTestEnv;
using endo::testing::unsetTestEnv;

// =============================================================================
// ProviderFactory tests
// =============================================================================

TEST_CASE("agent.factory.no_keys_no_providers")
{
    // Ensure no relevant API keys are set
    unsetTestEnv("ENDO_TEST_CLAUDE_KEY");
    unsetTestEnv("ENDO_TEST_OPENAI_KEY");
    unsetTestEnv("ENDO_TEST_GEMINI_KEY");

    auto config = AgentConfig {};
    config.claude.apiKeyEnv = "ENDO_TEST_CLAUDE_KEY";
    config.claude.authPreference = "api_key"; // Skip OAuth store (may have real credentials on dev machine)
    config.openai.apiKeyEnv = "ENDO_TEST_OPENAI_KEY";
    config.gemini.apiKeyEnv = "ENDO_TEST_GEMINI_KEY";
    config.gemini.authPreference = "api_key"; // Skip OAuth store (may have real credentials on dev machine)
    config.openaiCompat.baseUrl = "";         // no compat provider

    endo::http::HttpClient httpClient;
    auto factory = ProviderFactory(httpClient, config);

    CHECK(factory.activeProvider() == nullptr);
    CHECK(factory.authenticatedProviders().empty());
}

TEST_CASE("agent.factory.single_provider")
{
    setTestEnv("ENDO_TEST_FACTORY_KEY", "test-key-value");

    auto config = AgentConfig {};
    config.activeProvider = "claude";
    config.claude.apiKeyEnv = "ENDO_TEST_FACTORY_KEY";
    config.openai.apiKeyEnv = "ENDO_TEST_NONEXISTENT_1";
    config.gemini.apiKeyEnv = "ENDO_TEST_NONEXISTENT_2";
    config.gemini.authPreference = "api_key"; // Skip OAuth store (may have real credentials on dev machine)
    config.openaiCompat.baseUrl = "";

    endo::http::HttpClient httpClient;
    auto factory = ProviderFactory(httpClient, config);

    CHECK(factory.activeProvider() != nullptr);
    CHECK(factory.activeProviderName() == "claude");
    CHECK(factory.authenticatedProviders().size() == 1);

    unsetTestEnv("ENDO_TEST_FACTORY_KEY");
}

TEST_CASE("agent.factory.switch_provider")
{
    setTestEnv("ENDO_TEST_SWITCH_CLAUDE", "key1");
    setTestEnv("ENDO_TEST_SWITCH_GEMINI", "key2");

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

    unsetTestEnv("ENDO_TEST_SWITCH_CLAUDE");
    unsetTestEnv("ENDO_TEST_SWITCH_GEMINI");
}

TEST_CASE("agent.factory.no_fallback_when_explicit_provider_not_available")
{
    setTestEnv("ENDO_TEST_FALLBACK_KEY", "test-key");

    auto config = AgentConfig {};
    config.activeProvider = "claude"; // Claude key not available
    config.claude.apiKeyEnv = "ENDO_TEST_NONEXISTENT_4";
    config.claude.authPreference = "api_key"; // Skip OAuth store (may have real credentials on dev machine)
    config.openai.apiKeyEnv = "ENDO_TEST_FALLBACK_KEY";
    config.gemini.apiKeyEnv = "ENDO_TEST_NONEXISTENT_5";
    config.gemini.authPreference = "api_key"; // Skip OAuth store (may have real credentials on dev machine)
    config.openaiCompat.baseUrl = "";

    endo::http::HttpClient httpClient;
    auto factory = ProviderFactory(httpClient, config);

    // Explicit provider not available — must NOT silently fall back to another.
    CHECK(factory.activeProvider() == nullptr);
    CHECK(factory.activeProviderName().empty());

    unsetTestEnv("ENDO_TEST_FALLBACK_KEY");
}

TEST_CASE("agent.factory.auto_detect_when_no_preference")
{
    setTestEnv("ENDO_TEST_AUTODETECT_KEY", "test-key");

    auto config = AgentConfig {};
    config.activeProvider = ""; // No explicit preference — auto-detect.
    config.claude.apiKeyEnv = "ENDO_TEST_NONEXISTENT_9";
    config.claude.authPreference = "api_key";
    config.openai.apiKeyEnv = "ENDO_TEST_AUTODETECT_KEY";
    config.gemini.apiKeyEnv = "ENDO_TEST_NONEXISTENT_10";
    config.gemini.authPreference = "api_key";
    config.openaiCompat.baseUrl = "";

    endo::http::HttpClient httpClient;
    auto factory = ProviderFactory(httpClient, config);

    // No preference set — should auto-detect the first authenticated provider.
    CHECK(factory.activeProvider() != nullptr);
    CHECK(!factory.activeProviderName().empty());

    unsetTestEnv("ENDO_TEST_AUTODETECT_KEY");
}

TEST_CASE("agent.factory.openai_compat_no_key_required")
{
    unsetTestEnv("ENDO_TEST_COMPAT_KEY");

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
