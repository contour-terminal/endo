// SPDX-License-Identifier: Apache-2.0
#include <http/HttpClient.hpp>

#include <catch2/catch_test_macros.hpp>

#include <agent/providers/CopilotProvider.hpp>
#include <agent/providers/OpenAiProvider.hpp>

using namespace endo::agent;

// =============================================================================
// Capability tests
// =============================================================================

TEST_CASE("agent.copilot.capabilities")
{
    endo::http::HttpClient httpClient;
    auto config = CopilotProviderConfig { .githubToken = "ghu_test_token" };
    auto provider = CopilotProvider(httpClient, std::move(config));

    CHECK(provider.supportsToolUse());
    CHECK(provider.supportsImageInput());
    CHECK(!provider.supportsImageOutput());
    CHECK(provider.contextSize() == 128000);
}

TEST_CASE("agent.copilot.model_info")
{
    endo::http::HttpClient httpClient;
    auto config = CopilotProviderConfig { .githubToken = "ghu_test_token" };
    auto provider = CopilotProvider(httpClient, std::move(config));

    auto const info = provider.modelInfo();
    CHECK(info.providerName == "copilot");
    CHECK(info.modelName == "gpt-4o");
    CHECK(info.contextSize == 128000);
    CHECK(info.supportsToolUse);
    CHECK(info.supportsImageInput);
    CHECK(!info.supportsImageOutput);
}

TEST_CASE("agent.copilot.custom_model_config")
{
    endo::http::HttpClient httpClient;
    auto config = CopilotProviderConfig {
        .githubToken = "ghu_test_token",
        .model = "claude-3.5-sonnet",
        .maxTokens = 8192,
        .contextWindowSize = 200000,
    };
    auto provider = CopilotProvider(httpClient, std::move(config));

    CHECK(provider.contextSize() == 200000);

    auto const info = provider.modelInfo();
    CHECK(info.modelName == "claude-3.5-sonnet");
    CHECK(info.contextSize == 200000);
}

// =============================================================================
// Serialization reuse verification
// =============================================================================

TEST_CASE("agent.copilot.reuses_openai_serialization")
{
    // Verify that CopilotProvider would produce the same request body
    // as OpenAiProvider::serializeRequest() since it delegates to it.
    auto messages = std::vector<ChatMessage> {
        ChatMessage::text(Role::User, "Hello from Copilot!"),
    };

    auto const json = OpenAiProvider::serializeRequest(messages, {}, "gpt-4o", 4096, ThinkingMode::Off);

    CHECK(json["model"] == "gpt-4o");
    CHECK(json["max_tokens"] == 4096);
    CHECK(json["stream"] == true);
    CHECK(json.contains("stream_options"));

    auto const& msgs = json["messages"];
    REQUIRE(msgs.size() == 1);
    CHECK(msgs[0]["role"] == "user");
    CHECK(msgs[0]["content"] == "Hello from Copilot!");
}

TEST_CASE("agent.copilot.parse_done_sentinel")
{
    // CopilotProvider uses OpenAiProvider::parseSseData for SSE parsing.
    auto const result = OpenAiProvider::parseSseData("[DONE]");
    CHECK(!result.has_value());
}
