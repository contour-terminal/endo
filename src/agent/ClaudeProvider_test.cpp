// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <agent/ClaudeProvider.hpp>

using namespace endo::agent;
using namespace endo::http;

// =============================================================================
// Request serialization tests
// =============================================================================

TEST_CASE("agent.claude.serialize_simple_message")
{
    auto messages = std::vector<ChatMessage> {
        ChatMessage::text(Role::User, "Hello!"),
    };

    auto const json = ClaudeProvider::serializeRequest(messages, {}, "claude-sonnet-4-5-20250929", 1024);

    CHECK(json["model"] == "claude-sonnet-4-5-20250929");
    CHECK(json["max_tokens"] == 1024);
    CHECK(json["stream"] == true);
    CHECK(!json.contains("system"));
    CHECK(!json.contains("tools"));

    auto const& msgs = json["messages"];
    REQUIRE(msgs.size() == 1);
    CHECK(msgs[0]["role"] == "user");
    REQUIRE(msgs[0]["content"].size() == 1);
    CHECK(msgs[0]["content"][0]["type"] == "text");
    CHECK(msgs[0]["content"][0]["text"] == "Hello!");
}

TEST_CASE("agent.claude.serialize_system_message_extracted")
{
    auto messages = std::vector<ChatMessage> {
        ChatMessage::text(Role::System, "You are a helpful assistant."),
        ChatMessage::text(Role::User, "Hi"),
    };

    auto const json = ClaudeProvider::serializeRequest(messages, {}, "claude-sonnet-4-5-20250929", 1024);

    CHECK(json["system"] == "You are a helpful assistant.");

    auto const& msgs = json["messages"];
    REQUIRE(msgs.size() == 1);
    CHECK(msgs[0]["role"] == "user");
}

TEST_CASE("agent.claude.serialize_multiple_system_messages")
{
    auto messages = std::vector<ChatMessage> {
        ChatMessage::text(Role::System, "First instruction."),
        ChatMessage::text(Role::System, "Second instruction."),
        ChatMessage::text(Role::User, "Hello"),
    };

    auto const json = ClaudeProvider::serializeRequest(messages, {}, "claude-sonnet-4-5-20250929", 1024);
    CHECK(json["system"] == "First instruction.\nSecond instruction.");
}

TEST_CASE("agent.claude.serialize_with_tools")
{
    auto messages = std::vector<ChatMessage> {
        ChatMessage::text(Role::User, "Search for cats"),
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

    auto const json = ClaudeProvider::serializeRequest(messages, tools, "claude-sonnet-4-5-20250929", 1024);

    REQUIRE(json.contains("tools"));
    auto const& toolsArr = json["tools"];
    REQUIRE(toolsArr.size() == 1);
    CHECK(toolsArr[0]["name"] == "search");
    CHECK(toolsArr[0]["description"] == "Search the web");
    CHECK(toolsArr[0]["input_schema"]["type"] == "object");
}

TEST_CASE("agent.claude.serialize_tool_use_block")
{
    auto msg = ChatMessage { .role = Role::Assistant };
    msg.content.emplace_back(ToolUseBlock {
        .id = "toolu_123",
        .name = "search",
        .arguments = nlohmann::json { { "query", "cats" } },
    });

    auto messages = std::vector<ChatMessage> { std::move(msg) };
    auto const json = ClaudeProvider::serializeRequest(messages, {}, "claude-sonnet-4-5-20250929", 1024);

    auto const& content = json["messages"][0]["content"];
    REQUIRE(content.size() == 1);
    CHECK(content[0]["type"] == "tool_use");
    CHECK(content[0]["id"] == "toolu_123");
    CHECK(content[0]["name"] == "search");
    CHECK(content[0]["input"]["query"] == "cats");
}

TEST_CASE("agent.claude.serialize_tool_result_block")
{
    auto msg = ChatMessage { .role = Role::User };
    msg.content.emplace_back(ToolResultBlock {
        .toolUseId = "toolu_123",
        .content = "Found 42 cats",
        .isError = false,
    });

    auto messages = std::vector<ChatMessage> { std::move(msg) };
    auto const json = ClaudeProvider::serializeRequest(messages, {}, "claude-sonnet-4-5-20250929", 1024);

    auto const& content = json["messages"][0]["content"];
    REQUIRE(content.size() == 1);
    CHECK(content[0]["type"] == "tool_result");
    CHECK(content[0]["tool_use_id"] == "toolu_123");
    CHECK(content[0]["content"] == "Found 42 cats");
}

// =============================================================================
// SSE event parsing tests
// =============================================================================

TEST_CASE("agent.claude.parse_message_start")
{
    auto accumulators = std::vector<ContentBlockAccumulator> {};
    auto event = SseEvent { .event = "message_start", .data = "{}" };

    auto result = ClaudeProvider::parseSseEvent(event, accumulators);
    REQUIRE(result.has_value());
    CHECK(!result->done);
    CHECK(result->textDelta.empty());
}

TEST_CASE("agent.claude.parse_message_stop")
{
    auto accumulators = std::vector<ContentBlockAccumulator> {};
    auto event = SseEvent { .event = "message_stop", .data = "" };

    auto result = ClaudeProvider::parseSseEvent(event, accumulators);
    REQUIRE(result.has_value());
    CHECK(result->done);
}

TEST_CASE("agent.claude.parse_text_block_lifecycle")
{
    auto accumulators = std::vector<ContentBlockAccumulator> {};

    // content_block_start
    {
        auto event = SseEvent {
            .event = "content_block_start",
            .data = R"({"index": 0, "content_block": {"type": "text", "text": ""}})",
        };
        auto result = ClaudeProvider::parseSseEvent(event, accumulators);
        REQUIRE(result.has_value());
        REQUIRE(accumulators.size() == 1);
        CHECK(accumulators[0].type == "text");
    }

    // content_block_delta (text)
    {
        auto event = SseEvent {
            .event = "content_block_delta",
            .data = R"({"index": 0, "delta": {"type": "text_delta", "text": "Hello "}})",
        };
        auto result = ClaudeProvider::parseSseEvent(event, accumulators);
        REQUIRE(result.has_value());
        CHECK(result->textDelta == "Hello ");
    }

    // Another delta
    {
        auto event = SseEvent {
            .event = "content_block_delta",
            .data = R"({"index": 0, "delta": {"type": "text_delta", "text": "world!"}})",
        };
        auto result = ClaudeProvider::parseSseEvent(event, accumulators);
        REQUIRE(result.has_value());
        CHECK(result->textDelta == "world!");
    }

    // content_block_stop
    {
        auto event = SseEvent {
            .event = "content_block_stop",
            .data = R"({"index": 0})",
        };
        auto result = ClaudeProvider::parseSseEvent(event, accumulators);
        REQUIRE(result.has_value());
        REQUIRE(result->completedBlocks.size() == 1);
        auto const* text = std::get_if<TextBlock>(&result->completedBlocks[0]);
        REQUIRE(text != nullptr);
        CHECK(text->text == "Hello world!");
    }
}

TEST_CASE("agent.claude.parse_tool_use_lifecycle")
{
    auto accumulators = std::vector<ContentBlockAccumulator> {};

    // content_block_start (tool_use)
    {
        auto event = SseEvent {
            .event = "content_block_start",
            .data =
                R"({"index": 0, "content_block": {"type": "tool_use", "id": "toolu_abc", "name": "search"}})",
        };
        auto result = ClaudeProvider::parseSseEvent(event, accumulators);
        REQUIRE(result.has_value());
        CHECK(accumulators[0].type == "tool_use");
        CHECK(accumulators[0].toolId == "toolu_abc");
        CHECK(accumulators[0].toolName == "search");
    }

    // content_block_delta (input_json_delta)
    {
        auto event = SseEvent {
            .event = "content_block_delta",
            .data = R"({"index": 0, "delta": {"type": "input_json_delta", "partial_json": "{\"query\":"}})",
        };
        auto result = ClaudeProvider::parseSseEvent(event, accumulators);
        REQUIRE(result.has_value());
    }

    // More JSON delta
    {
        auto event = SseEvent {
            .event = "content_block_delta",
            .data = R"({"index": 0, "delta": {"type": "input_json_delta", "partial_json": " \"cats\"}"}})",
        };
        auto result = ClaudeProvider::parseSseEvent(event, accumulators);
        REQUIRE(result.has_value());
    }

    // content_block_stop
    {
        auto event = SseEvent {
            .event = "content_block_stop",
            .data = R"({"index": 0})",
        };
        auto result = ClaudeProvider::parseSseEvent(event, accumulators);
        REQUIRE(result.has_value());
        REQUIRE(result->completedToolCalls.size() == 1);
        CHECK(result->completedToolCalls[0].id == "toolu_abc");
        CHECK(result->completedToolCalls[0].name == "search");
        CHECK(result->completedToolCalls[0].arguments["query"] == "cats");
    }
}

TEST_CASE("agent.claude.parse_ping_event")
{
    auto accumulators = std::vector<ContentBlockAccumulator> {};
    auto event = SseEvent { .event = "ping", .data = "" };
    auto result = ClaudeProvider::parseSseEvent(event, accumulators);
    REQUIRE(result.has_value());
    CHECK(!result->done);
}

// =============================================================================
// Capability tests
// =============================================================================

TEST_CASE("agent.claude.capabilities")
{
    endo::http::HttpClient httpClient;
    auto config = ClaudeProviderConfig { .apiKey = "test-key" };
    auto provider = ClaudeProvider(httpClient, std::move(config));

    CHECK(provider.supportsToolUse());
    CHECK(provider.supportsImageInput());
    CHECK(!provider.supportsImageOutput());
    CHECK(provider.contextSize() == 200000);

    auto info = provider.modelInfo();
    CHECK(info.providerName == "claude");
    CHECK(info.modelName == "claude-sonnet-4-5-20250929");
}
