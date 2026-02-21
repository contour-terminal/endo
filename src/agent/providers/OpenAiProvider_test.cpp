// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <agent/providers/OpenAiProvider.hpp>

using namespace endo::agent;

// =============================================================================
// Request serialization tests
// =============================================================================

TEST_CASE("agent.openai.serialize_simple_message")
{
    auto messages = std::vector<ChatMessage> {
        ChatMessage::text(Role::User, "Hello!"),
    };

    auto const json = OpenAiProvider::serializeRequest(messages, {}, "gpt-4o", 1024, ThinkingMode::Off);

    CHECK(json["model"] == "gpt-4o");
    CHECK(json["max_tokens"] == 1024);
    CHECK(json["stream"] == true);
    CHECK(json.contains("stream_options"));
    CHECK(!json.contains("tools"));

    auto const& msgs = json["messages"];
    REQUIRE(msgs.size() == 1);
    CHECK(msgs[0]["role"] == "user");
    // Simple text uses string content, not array
    CHECK(msgs[0]["content"].is_string());
    CHECK(msgs[0]["content"] == "Hello!");
}

TEST_CASE("agent.openai.serialize_system_message_inline")
{
    auto messages = std::vector<ChatMessage> {
        ChatMessage::text(Role::System, "You are helpful."),
        ChatMessage::text(Role::User, "Hi"),
    };

    auto const json = OpenAiProvider::serializeRequest(messages, {}, "gpt-4o", 1024, ThinkingMode::Off);

    // System messages are inline (not extracted like Claude)
    CHECK(!json.contains("system"));
    auto const& msgs = json["messages"];
    REQUIRE(msgs.size() == 2);
    CHECK(msgs[0]["role"] == "system");
    CHECK(msgs[0]["content"] == "You are helpful.");
    CHECK(msgs[1]["role"] == "user");
}

TEST_CASE("agent.openai.serialize_with_tools")
{
    auto messages = std::vector<ChatMessage> {
        ChatMessage::text(Role::User, "Search"),
    };

    auto tools = std::vector<ToolDefinition> {
        { .name = "search",
          .description = "Search the web",
          .inputSchema =
              nlohmann::json {
                  { "type", "object" },
                  { "properties", { { "query", { { "type", "string" } } } } },
              } },
    };

    auto const json = OpenAiProvider::serializeRequest(messages, tools, "gpt-4o", 1024, ThinkingMode::Off);

    REQUIRE(json.contains("tools"));
    auto const& toolsArr = json["tools"];
    REQUIRE(toolsArr.size() == 1);
    CHECK(toolsArr[0]["type"] == "function");
    CHECK(toolsArr[0]["function"]["name"] == "search");
    CHECK(toolsArr[0]["function"]["description"] == "Search the web");
    CHECK(toolsArr[0]["function"]["parameters"]["type"] == "object");
}

TEST_CASE("agent.openai.serialize_tool_calls_in_assistant")
{
    auto msg = ChatMessage { .role = Role::Assistant };
    msg.content.emplace_back(TextBlock { .text = "Let me search" });
    msg.content.emplace_back(ToolUseBlock {
        .id = "call_123",
        .name = "search",
        .arguments = nlohmann::json { { "query", "cats" } },
    });

    auto messages = std::vector<ChatMessage> { std::move(msg) };
    auto const json = OpenAiProvider::serializeRequest(messages, {}, "gpt-4o", 1024, ThinkingMode::Off);

    auto const& assistantMsg = json["messages"][0];
    CHECK(assistantMsg["role"] == "assistant");
    CHECK(assistantMsg["content"] == "Let me search");
    REQUIRE(assistantMsg.contains("tool_calls"));
    auto const& tc = assistantMsg["tool_calls"][0];
    CHECK(tc["id"] == "call_123");
    CHECK(tc["type"] == "function");
    CHECK(tc["function"]["name"] == "search");
}

TEST_CASE("agent.openai.serialize_tool_result")
{
    auto msg = ChatMessage { .role = Role::Tool };
    msg.content.emplace_back(ToolResultBlock {
        .toolUseId = "call_123",
        .content = "Found 42 cats",
    });

    auto messages = std::vector<ChatMessage> { std::move(msg) };
    auto const json = OpenAiProvider::serializeRequest(messages, {}, "gpt-4o", 1024, ThinkingMode::Off);

    auto const& toolMsg = json["messages"][0];
    CHECK(toolMsg["role"] == "tool");
    CHECK(toolMsg["tool_call_id"] == "call_123");
    CHECK(toolMsg["content"] == "Found 42 cats");
}

// =============================================================================
// SSE data parsing tests
// =============================================================================

TEST_CASE("agent.openai.parse_done_sentinel")
{
    auto result = OpenAiProvider::parseSseData("[DONE]");
    CHECK(!result.has_value());
}

TEST_CASE("agent.openai.parse_text_delta")
{
    auto const data = R"({"choices":[{"delta":{"content":"Hello"},"index":0}]})";
    auto result = OpenAiProvider::parseSseData(data);
    REQUIRE(result.has_value());
    CHECK(result->contains("choices"));
    CHECK((*result)["choices"][0]["delta"]["content"] == "Hello");
}

TEST_CASE("agent.openai.parse_tool_call_delta")
{
    auto const data = R"({
        "choices": [{
            "delta": {
                "tool_calls": [{
                    "index": 0,
                    "id": "call_abc",
                    "function": {
                        "name": "search",
                        "arguments": "{\"query\":"
                    }
                }]
            },
            "index": 0
        }]
    })";
    auto result = OpenAiProvider::parseSseData(data);
    REQUIRE(result.has_value());
    auto const& tc = (*result)["choices"][0]["delta"]["tool_calls"][0];
    CHECK(tc["id"] == "call_abc");
    CHECK(tc["function"]["name"] == "search");
}

TEST_CASE("agent.openai.parse_malformed_json")
{
    auto result = OpenAiProvider::parseSseData("{invalid json");
    // Should return a discarded json, not crash
    REQUIRE(result.has_value());
    CHECK(result->is_discarded());
}

// =============================================================================
// Usage parsing tests
// =============================================================================

TEST_CASE("agent.openai.parse_usage_in_final_chunk")
{
    // Simulate the final chunk from OpenAI that contains usage data.
    auto data = R"({
        "id": "chatcmpl-abc",
        "choices": [],
        "usage": {
            "prompt_tokens": 1500,
            "completion_tokens": 300,
            "prompt_tokens_details": {
                "cached_tokens": 400
            }
        }
    })";
    auto parsed = OpenAiProvider::parseSseData(data);
    REQUIRE(parsed.has_value());
    auto const& json = *parsed;
    REQUIRE(json.contains("usage"));

    auto const& u = json["usage"];
    CHECK(u["prompt_tokens"].get<int64_t>() == 1500);
    CHECK(u["completion_tokens"].get<int64_t>() == 300);
    CHECK(u["prompt_tokens_details"]["cached_tokens"].get<int64_t>() == 400);
}

TEST_CASE("agent.openai.parse_usage_without_cache_details")
{
    auto data = R"({
        "choices": [],
        "usage": {
            "prompt_tokens": 800,
            "completion_tokens": 200
        }
    })";
    auto parsed = OpenAiProvider::parseSseData(data);
    REQUIRE(parsed.has_value());
    auto const& json = *parsed;
    REQUIRE(json.contains("usage"));
    CHECK(!json["usage"].contains("prompt_tokens_details"));
}

// =============================================================================
// Capability tests
// =============================================================================

TEST_CASE("agent.openai.capabilities")
{
    endo::http::HttpClient httpClient;
    auto config = OpenAiProviderConfig { .apiKey = "test-key" };
    auto provider = OpenAiProvider(httpClient, std::move(config));

    CHECK(provider.supportsToolUse());
    CHECK(provider.supportsImageInput());
    CHECK(!provider.supportsImageOutput());
    CHECK(provider.contextSize() == 128000);

    auto info = provider.modelInfo();
    CHECK(info.providerName == "openai");
    CHECK(info.modelName == "gpt-4o");
}

TEST_CASE("agent.openai.capabilities_custom")
{
    endo::http::HttpClient httpClient;
    auto config = OpenAiProviderConfig {
        .apiKey = "",
        .model = "llama3",
        .baseUrl = "http://localhost:11434/v1",
        .supportsImages = false,
        .supportsTools = false,
    };
    auto provider = OpenAiProvider(httpClient, std::move(config));

    CHECK(!provider.supportsToolUse());
    CHECK(!provider.supportsImageInput());
    CHECK(!provider.supportsImageOutput());
}
