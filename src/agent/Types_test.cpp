// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <agent/Types.hpp>

using namespace endo::agent;

// =============================================================================
// Role conversion tests
// =============================================================================

TEST_CASE("agent.types.role_to_string")
{
    CHECK(roleToString(Role::System) == "system");
    CHECK(roleToString(Role::User) == "user");
    CHECK(roleToString(Role::Assistant) == "assistant");
    CHECK(roleToString(Role::Tool) == "tool");
}

TEST_CASE("agent.types.role_from_string")
{
    CHECK(roleFromString("system") == Role::System);
    CHECK(roleFromString("user") == Role::User);
    CHECK(roleFromString("assistant") == Role::Assistant);
    CHECK(roleFromString("tool") == Role::Tool);
    CHECK(roleFromString("unknown") == Role::User); // fallback
}

TEST_CASE("agent.types.role_round_trip")
{
    for (auto role: { Role::System, Role::User, Role::Assistant, Role::Tool })
    {
        CHECK(roleFromString(roleToString(role)) == role);
    }
}

// =============================================================================
// ChatMessage tests
// =============================================================================

TEST_CASE("agent.types.chat_message_text_factory")
{
    auto msg = ChatMessage::text(Role::User, "Hello, world!");
    CHECK(msg.role == Role::User);
    CHECK(msg.content.size() == 1);
    CHECK(msg.textContent() == "Hello, world!");
}

TEST_CASE("agent.types.chat_message_text_content_multiple_blocks")
{
    auto msg = ChatMessage { .role = Role::Assistant };
    msg.content.emplace_back(TextBlock { .text = "First" });
    msg.content.emplace_back(ImageBlock { .data = { 0x89, 0x50 }, .mediaType = "image/png" });
    msg.content.emplace_back(TextBlock { .text = "Second" });

    CHECK(msg.textContent() == "First\nSecond");
}

TEST_CASE("agent.types.chat_message_text_content_empty")
{
    auto msg = ChatMessage { .role = Role::User };
    CHECK(msg.textContent().empty());
}

// =============================================================================
// GenerateResult tests
// =============================================================================

TEST_CASE("agent.types.generate_result_no_tool_calls")
{
    auto result = GenerateResult {};
    result.content.emplace_back(TextBlock { .text = "Response text" });

    CHECK(!result.hasToolCalls());
    CHECK(result.textContent() == "Response text");
}

TEST_CASE("agent.types.generate_result_with_tool_calls")
{
    auto result = GenerateResult {};
    result.content.emplace_back(TextBlock { .text = "Let me search" });
    result.toolCalls.push_back(ToolCall {
        .id = "tc_1",
        .name = "search",
        .arguments = nlohmann::json { { "query", "test" } },
    });

    CHECK(result.hasToolCalls());
    CHECK(result.toolCalls.size() == 1);
    CHECK(result.toolCalls[0].name == "search");
    CHECK(result.textContent() == "Let me search");
}

// =============================================================================
// Content block variant tests
// =============================================================================

TEST_CASE("agent.types.content_block_variant_text")
{
    ContentBlock block = TextBlock { .text = "hello" };
    CHECK(std::holds_alternative<TextBlock>(block));
    CHECK(std::get<TextBlock>(block).text == "hello");
}

TEST_CASE("agent.types.content_block_variant_image")
{
    ContentBlock block = ImageBlock { .data = { 1, 2, 3 }, .mediaType = "image/jpeg" };
    CHECK(std::holds_alternative<ImageBlock>(block));
    auto const& img = std::get<ImageBlock>(block);
    CHECK(img.data.size() == 3);
    CHECK(img.mediaType == "image/jpeg");
}

TEST_CASE("agent.types.content_block_variant_tool_use")
{
    ContentBlock block = ToolUseBlock {
        .id = "call_1",
        .name = "read_file",
        .arguments = nlohmann::json { { "path", "/tmp/test" } },
    };
    CHECK(std::holds_alternative<ToolUseBlock>(block));
    auto const& tool = std::get<ToolUseBlock>(block);
    CHECK(tool.name == "read_file");
    CHECK(tool.arguments["path"] == "/tmp/test");
}

TEST_CASE("agent.types.content_block_variant_tool_result")
{
    ContentBlock block = ToolResultBlock {
        .toolUseId = "call_1",
        .content = "file contents here",
        .isError = false,
    };
    CHECK(std::holds_alternative<ToolResultBlock>(block));
    auto const& result = std::get<ToolResultBlock>(block);
    CHECK(result.toolUseId == "call_1");
    CHECK(!result.isError);
}
