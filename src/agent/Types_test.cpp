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

// =============================================================================
// TokenUsage tests
// =============================================================================

TEST_CASE("agent.types.token_usage_default_zero")
{
    auto usage = TokenUsage {};
    CHECK(usage.inputTokens == 0);
    CHECK(usage.outputTokens == 0);
    CHECK(usage.cacheReadTokens == 0);
    CHECK(usage.cacheCreationTokens == 0);
}

TEST_CASE("agent.types.token_usage_accumulate")
{
    auto total = TokenUsage {};
    auto turn1 = TokenUsage { .inputTokens = 100, .outputTokens = 50, .cacheReadTokens = 20 };
    auto turn2 = TokenUsage { .inputTokens = 200, .outputTokens = 75, .cacheCreationTokens = 30 };
    total += turn1;
    total += turn2;
    CHECK(total.inputTokens == 300);
    CHECK(total.outputTokens == 125);
    CHECK(total.cacheReadTokens == 20);
    CHECK(total.cacheCreationTokens == 30);
}

// =============================================================================
// estimateCost tests
// =============================================================================

TEST_CASE("agent.types.estimate_cost_claude_sonnet")
{
    auto usage = TokenUsage { .inputTokens = 1'000'000, .outputTokens = 1'000'000 };
    auto cost = estimateCost(usage, "claude", "claude-sonnet-4-6");
    // Sonnet: $3/M input + $15/M output = $18
    CHECK(cost > 17.0);
    CHECK(cost < 19.0);
}

TEST_CASE("agent.types.estimate_cost_claude_opus")
{
    auto usage = TokenUsage { .inputTokens = 1'000'000, .outputTokens = 1'000'000 };
    auto cost = estimateCost(usage, "claude", "claude-opus-4-6");
    // Opus: $15/M input + $75/M output = $90
    CHECK(cost > 89.0);
    CHECK(cost < 91.0);
}

TEST_CASE("agent.types.estimate_cost_claude_with_cache")
{
    auto usage = TokenUsage {
        .inputTokens = 1'000'000,
        .outputTokens = 100'000,
        .cacheReadTokens = 800'000,
    };
    // 200k regular input at $3/M = $0.60
    // 800k cache read at $0.30/M (10% of $3) = $0.24
    // 100k output at $15/M = $1.50
    auto cost = estimateCost(usage, "claude", "claude-sonnet-4-6");
    CHECK(cost > 2.0);
    CHECK(cost < 3.0);
}

TEST_CASE("agent.types.estimate_cost_openai_gpt4o")
{
    auto usage = TokenUsage { .inputTokens = 1'000'000, .outputTokens = 1'000'000 };
    auto cost = estimateCost(usage, "openai", "gpt-4o");
    // $2.50/M input + $10/M output = $12.50
    CHECK(cost > 12.0);
    CHECK(cost < 13.0);
}

TEST_CASE("agent.types.estimate_cost_unknown_model")
{
    auto usage = TokenUsage { .inputTokens = 1000, .outputTokens = 500 };
    auto cost = estimateCost(usage, "unknown", "unknown-model");
    CHECK(cost == 0.0);
}

// =============================================================================
// formatTokenCount tests
// =============================================================================

TEST_CASE("agent.types.format_token_count_small")
{
    CHECK(formatTokenCount(0) == "0");
    CHECK(formatTokenCount(42) == "42");
    CHECK(formatTokenCount(999) == "999");
}

TEST_CASE("agent.types.format_token_count_thousands")
{
    CHECK(formatTokenCount(1000) == "1.0k");
    CHECK(formatTokenCount(1234) == "1.2k");
    CHECK(formatTokenCount(9999) == "10.0k");
}

TEST_CASE("agent.types.format_token_count_large")
{
    CHECK(formatTokenCount(10000) == "10k");
    CHECK(formatTokenCount(123456) == "123k");
    CHECK(formatTokenCount(999999) == "1000k");
}

TEST_CASE("agent.types.format_token_count_millions")
{
    CHECK(formatTokenCount(1'000'000) == "1.0M");
    CHECK(formatTokenCount(2'500'000) == "2.5M");
}
