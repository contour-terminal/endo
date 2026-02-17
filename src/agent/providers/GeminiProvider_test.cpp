// SPDX-License-Identifier: Apache-2.0
#include <http/HttpClient.hpp>

#include <catch2/catch_test_macros.hpp>

#include <agent/providers/GeminiProvider.hpp>

using namespace endo::agent;

// =============================================================================
// serializeRequest tests
// =============================================================================

TEST_CASE("agent.gemini.serialize_simple_user_message")
{
    auto messages = std::vector<ChatMessage> { ChatMessage::text(Role::User, "Hello") };
    auto const json = GeminiProvider::serializeRequest(messages, {}, 1024);

    CHECK(json.contains("contents"));
    CHECK(json["contents"].size() == 1);
    CHECK(json["contents"][0]["role"] == "user");
    CHECK(json["contents"][0]["parts"][0]["text"] == "Hello");
    CHECK(json["generationConfig"]["maxOutputTokens"] == 1024);
    CHECK(!json.contains("systemInstruction"));
    CHECK(!json.contains("tools"));
}

TEST_CASE("agent.gemini.serialize_system_message")
{
    auto messages = std::vector<ChatMessage> {
        ChatMessage::text(Role::System, "You are a helpful assistant."),
        ChatMessage::text(Role::User, "Hi"),
    };
    auto const json = GeminiProvider::serializeRequest(messages, {}, 2048);

    CHECK(json.contains("systemInstruction"));
    CHECK(json["systemInstruction"]["parts"][0]["text"] == "You are a helpful assistant.");
    CHECK(json["contents"].size() == 1);
    CHECK(json["contents"][0]["role"] == "user");
}

TEST_CASE("agent.gemini.serialize_assistant_role_as_model")
{
    auto messages = std::vector<ChatMessage> {
        ChatMessage::text(Role::User, "Hello"),
        ChatMessage::text(Role::Assistant, "Hi there!"),
        ChatMessage::text(Role::User, "How are you?"),
    };
    auto const json = GeminiProvider::serializeRequest(messages, {}, 4096);

    CHECK(json["contents"].size() == 3);
    CHECK(json["contents"][0]["role"] == "user");
    CHECK(json["contents"][1]["role"] == "model");
    CHECK(json["contents"][1]["parts"][0]["text"] == "Hi there!");
    CHECK(json["contents"][2]["role"] == "user");
}

TEST_CASE("agent.gemini.serialize_multiple_system_messages")
{
    auto messages = std::vector<ChatMessage> {
        ChatMessage::text(Role::System, "First instruction."),
        ChatMessage::text(Role::System, "Second instruction."),
        ChatMessage::text(Role::User, "Go"),
    };
    auto const json = GeminiProvider::serializeRequest(messages, {}, 1024);

    CHECK(json["systemInstruction"]["parts"][0]["text"] == "First instruction.\nSecond instruction.");
    CHECK(json["contents"].size() == 1);
}

TEST_CASE("agent.gemini.serialize_image_block")
{
    auto msg = ChatMessage { .role = Role::User };
    msg.content.emplace_back(TextBlock { .text = "What is this?" });
    msg.content.emplace_back(ImageBlock { .data = { 0x89, 0x50, 0x4E, 0x47 }, .mediaType = "image/png" });

    auto messages = std::vector<ChatMessage> { std::move(msg) };
    auto const json = GeminiProvider::serializeRequest(messages, {}, 1024);

    auto const& parts = json["contents"][0]["parts"];
    CHECK(parts.size() == 2);
    CHECK(parts[0].contains("text"));
    CHECK(parts[1].contains("inlineData"));
    CHECK(parts[1]["inlineData"]["mimeType"] == "image/png");
    CHECK(!parts[1]["inlineData"]["data"].get<std::string>().empty());
}

TEST_CASE("agent.gemini.serialize_tool_use_block")
{
    auto msg = ChatMessage { .role = Role::Assistant };
    msg.content.emplace_back(ToolUseBlock {
        .id = "call_0",
        .name = "read_file",
        .arguments = nlohmann::json { { "path", "/tmp/test.txt" } },
    });

    auto messages = std::vector<ChatMessage> { std::move(msg) };
    auto const json = GeminiProvider::serializeRequest(messages, {}, 1024);

    auto const& parts = json["contents"][0]["parts"];
    CHECK(parts.size() == 1);
    CHECK(parts[0].contains("functionCall"));
    CHECK(parts[0]["functionCall"]["name"] == "read_file");
    CHECK(parts[0]["functionCall"]["args"]["path"] == "/tmp/test.txt");
}

TEST_CASE("agent.gemini.serialize_tool_result_block")
{
    // First an assistant message with a tool use, then a tool result.
    auto assistantMsg = ChatMessage { .role = Role::Assistant };
    assistantMsg.content.emplace_back(ToolUseBlock {
        .id = "call_42",
        .name = "search",
        .arguments = nlohmann::json { { "query", "test" } },
    });

    auto toolMsg = ChatMessage { .role = Role::Tool };
    toolMsg.content.emplace_back(ToolResultBlock {
        .toolUseId = "call_42",
        .content = "Found 3 results",
        .isError = false,
    });

    auto messages = std::vector<ChatMessage> { std::move(assistantMsg), std::move(toolMsg) };
    auto const json = GeminiProvider::serializeRequest(messages, {}, 1024);

    // Tool result should be a "user" role with functionResponse.
    CHECK(json["contents"].size() == 2);
    auto const& toolResponse = json["contents"][1];
    CHECK(toolResponse["role"] == "user");
    CHECK(toolResponse["parts"][0].contains("functionResponse"));
    CHECK(toolResponse["parts"][0]["functionResponse"]["name"] == "search");
    CHECK(toolResponse["parts"][0]["functionResponse"]["response"]["content"] == "Found 3 results");
}

TEST_CASE("agent.gemini.serialize_tool_result_fallback_name")
{
    // Tool result without a matching ToolUseBlock — should use toolUseId as fallback.
    auto toolMsg = ChatMessage { .role = Role::Tool };
    toolMsg.content.emplace_back(ToolResultBlock {
        .toolUseId = "unknown_call_id",
        .content = "result data",
        .isError = false,
    });

    auto messages = std::vector<ChatMessage> { std::move(toolMsg) };
    auto const json = GeminiProvider::serializeRequest(messages, {}, 1024);

    auto const& funcResp = json["contents"][0]["parts"][0]["functionResponse"];
    CHECK(funcResp["name"] == "unknown_call_id");
}

TEST_CASE("agent.gemini.serialize_tool_definitions")
{
    auto tools = std::vector<ToolDefinition> {
        ToolDefinition {
            .name = "get_weather",
            .description = "Get current weather",
            .inputSchema =
                nlohmann::json {
                    { "type", "object" },
                    { "properties", { { "city", { { "type", "string" } } } } },
                },
        },
        ToolDefinition {
            .name = "search",
            .description = "Search the web",
            .inputSchema = nlohmann::json {},
        },
    };

    auto messages = std::vector<ChatMessage> { ChatMessage::text(Role::User, "Weather?") };
    auto const json = GeminiProvider::serializeRequest(messages, tools, 1024);

    CHECK(json.contains("tools"));
    auto const& funcDecls = json["tools"][0]["functionDeclarations"];
    CHECK(funcDecls.size() == 2);
    CHECK(funcDecls[0]["name"] == "get_weather");
    CHECK(funcDecls[0]["description"] == "Get current weather");
    CHECK(funcDecls[0].contains("parameters"));
    // Second tool has empty schema — no parameters key.
    CHECK(funcDecls[1]["name"] == "search");
    CHECK(!funcDecls[1].contains("parameters"));
}

TEST_CASE("agent.gemini.serialize_no_tools_omits_key")
{
    auto messages = std::vector<ChatMessage> { ChatMessage::text(Role::User, "Hello") };
    auto const json = GeminiProvider::serializeRequest(messages, {}, 1024);

    CHECK(!json.contains("tools"));
}

TEST_CASE("agent.gemini.serialize_generation_config")
{
    auto messages = std::vector<ChatMessage> { ChatMessage::text(Role::User, "Hello") };
    auto const json = GeminiProvider::serializeRequest(messages, {}, 16384);

    CHECK(json["generationConfig"]["maxOutputTokens"] == 16384);
}

// =============================================================================
// Capability tests
// =============================================================================

TEST_CASE("agent.gemini.capabilities")
{
    auto config = GeminiProviderConfig {
        .apiKey = "test-key",
        .model = "gemini-2.5-pro",
        .maxTokens = 4096,
        .contextWindowSize = 2000000,
    };

    // We cannot construct the provider without a real HttpClient, but we can test
    // the static serialization. For capability tests, we need an instance.
    endo::http::HttpClient httpClient;
    auto provider = GeminiProvider(httpClient, config);

    CHECK(provider.supportsToolUse());
    CHECK(provider.supportsImageInput());
    CHECK(provider.supportsImageOutput());
    CHECK(provider.contextSize() == 2000000);

    auto const info = provider.modelInfo();
    CHECK(info.providerName == "gemini");
    CHECK(info.modelName == "gemini-2.5-pro");
    CHECK(info.contextSize == 2000000);
    CHECK(info.supportsToolUse);
    CHECK(info.supportsImageInput);
    CHECK(info.supportsImageOutput);
}

// =============================================================================
// Empty and edge-case tests
// =============================================================================

TEST_CASE("agent.gemini.serialize_empty_messages")
{
    auto const json = GeminiProvider::serializeRequest({}, {}, 1024);

    CHECK(json["contents"].empty());
    CHECK(!json.contains("systemInstruction"));
    CHECK(json["generationConfig"]["maxOutputTokens"] == 1024);
}

TEST_CASE("agent.gemini.serialize_message_with_empty_text_block")
{
    auto msg = ChatMessage { .role = Role::User };
    msg.content.emplace_back(TextBlock { .text = "" });

    auto messages = std::vector<ChatMessage> { std::move(msg) };
    auto const json = GeminiProvider::serializeRequest(messages, {}, 1024);

    // Even empty text blocks are serialized (Gemini API accepts them).
    CHECK(json["contents"].size() == 1);
    CHECK(json["contents"][0]["parts"][0]["text"] == "");
}

TEST_CASE("agent.gemini.serialize_mixed_content_message")
{
    auto msg = ChatMessage { .role = Role::User };
    msg.content.emplace_back(TextBlock { .text = "Look at this:" });
    msg.content.emplace_back(ImageBlock { .data = { 0xFF, 0xD8 }, .mediaType = "image/jpeg" });
    msg.content.emplace_back(TextBlock { .text = "What do you see?" });

    auto messages = std::vector<ChatMessage> { std::move(msg) };
    auto const json = GeminiProvider::serializeRequest(messages, {}, 1024);

    auto const& parts = json["contents"][0]["parts"];
    CHECK(parts.size() == 3);
    CHECK(parts[0]["text"] == "Look at this:");
    CHECK(parts[1].contains("inlineData"));
    CHECK(parts[1]["inlineData"]["mimeType"] == "image/jpeg");
    CHECK(parts[2]["text"] == "What do you see?");
}
