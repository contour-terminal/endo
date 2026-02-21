// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <format>
#include <locale>

#include <agent/providers/ProviderModels.hpp>

using namespace endo::agent;

// --- findModelByName tests ---

TEST_CASE("ProviderModels.findModelByName.exact_match", "[agent][providers][models]")
{
    auto const result = findModelByName("claude-sonnet-4-6");
    REQUIRE(result.has_value());
    CHECK(result->modelName == "claude-sonnet-4-6");
    CHECK(result->providerName == "claude");
}

TEST_CASE("ProviderModels.findModelByName.exact_match_case_insensitive", "[agent][providers][models]")
{
    auto const result = findModelByName("Claude-Sonnet-4-6");
    REQUIRE(result.has_value());
    CHECK(result->modelName == "claude-sonnet-4-6");
    CHECK(result->providerName == "claude");
}

TEST_CASE("ProviderModels.findModelByName.substring_match", "[agent][providers][models]")
{
    auto const result = findModelByName("sonnet");
    REQUIRE(result.has_value());
    // Should match the first sonnet model in the list.
    CHECK(result->modelName == "claude-sonnet-4-6");
    CHECK(result->providerName == "claude");
}

TEST_CASE("ProviderModels.findModelByName.substring_gemini", "[agent][providers][models]")
{
    auto const result = findModelByName("2.5-flash");
    REQUIRE(result.has_value());
    CHECK(result->modelName == "gemini-2.5-flash");
    CHECK(result->providerName == "gemini");
}

TEST_CASE("ProviderModels.findModelByName.preferred_provider_disambiguation", "[agent][providers][models]")
{
    // "4o" matches both "gpt-4o" and "gpt-4o-mini" in OpenAI.
    // With openai as preferred, should pick the first openai match.
    auto const result = findModelByName("4o", "openai");
    REQUIRE(result.has_value());
    CHECK(result->providerName == "openai");
    CHECK(result->modelName == "gpt-4o");
}

TEST_CASE("ProviderModels.findModelByName.no_match", "[agent][providers][models]")
{
    auto const result = findModelByName("nonexistent-model");
    CHECK_FALSE(result.has_value());
}

TEST_CASE("ProviderModels.findModelByName.empty_query", "[agent][providers][models]")
{
    auto const result = findModelByName("");
    CHECK_FALSE(result.has_value());
}

TEST_CASE("ProviderModels.findModelByName.openai_exact", "[agent][providers][models]")
{
    auto const result = findModelByName("gpt-4o");
    REQUIRE(result.has_value());
    CHECK(result->modelName == "gpt-4o");
    CHECK(result->providerName == "openai");
}

// --- allKnownModels tests ---

TEST_CASE("ProviderModels.allKnownModels.returns_all", "[agent][providers][models]")
{
    auto const models = allKnownModels();
    auto const expectedCount = ClaudeModels.size() + OpenAiModels.size() + GeminiModels.size();
    CHECK(models.size() == expectedCount);
}

TEST_CASE("ProviderModels.allKnownModels.provider_order", "[agent][providers][models]")
{
    auto const models = allKnownModels();
    // First models should be Claude, then OpenAI, then Gemini.
    CHECK(models.front().providerName == "claude");
    CHECK(models.back().providerName == "gemini");
}

// --- formatCapabilityDiff tests ---

TEST_CASE("ProviderModels.formatCapabilityDiff.same_capabilities", "[agent][providers][models]")
{
    auto const oldInfo = ModelInfo {
        .providerName = "claude",
        .modelName = "claude-sonnet-4-6",
        .contextSize = 200000,
        .supportsToolUse = true,
        .supportsImageInput = true,
        .supportsImageOutput = false,
    };
    auto newInfo = oldInfo;
    newInfo.modelName = "claude-opus-4-6";

    auto const result = formatCapabilityDiff(oldInfo, newInfo);
    CHECK(result.find("Switched from") != std::string::npos);
    CHECK(result.find("claude-sonnet-4-6") != std::string::npos);
    CHECK(result.find("claude-opus-4-6") != std::string::npos);
    // No table rows since capabilities are the same.
    CHECK(result.find("| Capability |") == std::string::npos);
}

TEST_CASE("ProviderModels.formatCapabilityDiff.different_context_size", "[agent][providers][models]")
{
    auto const oldInfo = ModelInfo {
        .providerName = "claude",
        .modelName = "claude-sonnet-4-6",
        .contextSize = 200000,
        .supportsToolUse = true,
        .supportsImageInput = true,
        .supportsImageOutput = false,
    };
    auto const newInfo = ModelInfo {
        .providerName = "openai",
        .modelName = "gpt-4o",
        .contextSize = 128000,
        .supportsToolUse = true,
        .supportsImageInput = true,
        .supportsImageOutput = false,
    };

    auto const result = formatCapabilityDiff(oldInfo, newInfo);
    CHECK(result.find("Context size") != std::string::npos);
    // Values are locale-formatted (e.g. "200,000" or "200.000" depending on locale).
    auto const oldFormatted = std::format(std::locale(""), "{:L}", size_t { 200000 });
    auto const newFormatted = std::format(std::locale(""), "{:L}", size_t { 128000 });
    CHECK(result.find(oldFormatted) != std::string::npos);
    CHECK(result.find(newFormatted) != std::string::npos);
}

TEST_CASE("ProviderModels.formatCapabilityDiff.cross_provider_all_changes", "[agent][providers][models]")
{
    auto const oldInfo = ModelInfo {
        .providerName = "claude",
        .modelName = "claude-sonnet-4-6",
        .contextSize = 200000,
        .supportsToolUse = true,
        .supportsImageInput = true,
        .supportsImageOutput = false,
    };
    auto const newInfo = ModelInfo {
        .providerName = "gemini",
        .modelName = "gemini-2.5-flash",
        .contextSize = 1000000,
        .supportsToolUse = true,
        .supportsImageInput = false,
        .supportsImageOutput = true,
    };

    auto const result = formatCapabilityDiff(oldInfo, newInfo);
    CHECK(result.find("Switched from") != std::string::npos);
    CHECK(result.find("Context size") != std::string::npos);
    CHECK(result.find("Image input") != std::string::npos);
    CHECK(result.find("Image output") != std::string::npos);
    // Tool use is the same, so it should NOT appear.
    CHECK(result.find("Tool use") == std::string::npos);
}
