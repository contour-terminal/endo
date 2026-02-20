// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <agent/conversation/TokenEstimator.hpp>

using namespace endo::agent;

TEST_CASE("TokenEstimator.empty_string", "[agent]")
{
    CHECK(estimateTokenCount(std::string_view {}) == 0);
}

TEST_CASE("TokenEstimator.single_character", "[agent]")
{
    CHECK(estimateTokenCount(std::string_view { "a" }) == 1);
}

TEST_CASE("TokenEstimator.english_text", "[agent]")
{
    // "Hello, how are you?" = 19 chars, ~4 chars/token = ~4-5 tokens
    auto const tokens = estimateTokenCount(std::string_view { "Hello, how are you?" });
    CHECK(tokens >= 4);
    CHECK(tokens <= 5);
}

TEST_CASE("TokenEstimator.code_text_uses_lower_divisor", "[agent]")
{
    // Code with lots of punctuation should use ~3.5 chars/token
    auto const code = std::string_view { "if (x == 0) { return foo(bar[i]); }" };
    auto const tokens = estimateTokenCount(code);
    // 36 chars / 3.5 ~= 10 tokens
    CHECK(tokens >= 9);
    CHECK(tokens <= 11);
}

TEST_CASE("TokenEstimator.chat_message_text_block", "[agent]")
{
    auto const msg = ChatMessage::text(Role::User, "Hello world");
    auto const tokens = estimateTokenCount(msg);
    // 4 overhead + ~2-3 for "Hello world" (11 chars / 4)
    CHECK(tokens >= 6);
    CHECK(tokens <= 8);
}

TEST_CASE("TokenEstimator.chat_message_image_block", "[agent]")
{
    auto msg = ChatMessage { .role = Role::User };
    msg.content.emplace_back(ImageBlock { .data = { 0x89, 0x50 }, .mediaType = "image/png" });
    auto const tokens = estimateTokenCount(msg);
    // 4 overhead + 1000 for image
    CHECK(tokens == 1004);
}

TEST_CASE("TokenEstimator.chat_message_tool_use_block", "[agent]")
{
    auto msg = ChatMessage { .role = Role::Assistant };
    msg.content.emplace_back(ToolUseBlock {
        .id = "call-1",
        .name = "read_file",
        .arguments = nlohmann::json { { "path", "/test.txt" } },
    });
    auto const tokens = estimateTokenCount(msg);
    // 4 overhead + name tokens + arguments tokens
    CHECK(tokens > 4);
}

TEST_CASE("TokenEstimator.chat_message_tool_result_block", "[agent]")
{
    auto msg = ChatMessage { .role = Role::User };
    msg.content.emplace_back(ToolResultBlock {
        .toolUseId = "call-1",
        .content = "file contents here",
    });
    auto const tokens = estimateTokenCount(msg);
    // 4 overhead + content tokens
    CHECK(tokens > 4);
}

TEST_CASE("TokenEstimator.multiple_messages", "[agent]")
{
    auto messages = std::vector<ChatMessage> {};
    messages.push_back(ChatMessage::text(Role::User, "Hello"));
    messages.push_back(ChatMessage::text(Role::Assistant, "Hi there, how can I help?"));

    auto const total = estimateTokenCount(std::span<ChatMessage const>(messages));
    auto const individual = estimateTokenCount(messages[0]) + estimateTokenCount(messages[1]);
    CHECK(total == individual);
}
