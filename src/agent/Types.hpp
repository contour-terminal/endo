// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

namespace endo::agent
{

/// Role of a participant in a chat conversation.
enum class Role : uint8_t
{
    System,
    User,
    Assistant,
    Tool,
};

/// Converts a Role enum to its string representation.
[[nodiscard]] constexpr auto roleToString(Role role) noexcept -> std::string_view
{
    switch (role)
    {
        case Role::System: return "system";
        case Role::User: return "user";
        case Role::Assistant: return "assistant";
        case Role::Tool: return "tool";
    }
    return "unknown";
}

/// Parses a string into a Role enum.
/// @return The matching Role, or Role::User if the string is unrecognized.
[[nodiscard]] constexpr auto roleFromString(std::string_view str) noexcept -> Role
{
    if (str == "system")
        return Role::System;
    if (str == "user")
        return Role::User;
    if (str == "assistant")
        return Role::Assistant;
    if (str == "tool")
        return Role::Tool;
    return Role::User;
}

/// A block of plain text content.
struct TextBlock
{
    std::string text;
};

/// A block containing raw image bytes (PNG/JPEG).
struct ImageBlock
{
    std::vector<uint8_t> data; ///< Raw image bytes.
    std::string mediaType;     ///< MIME type (e.g. "image/png", "image/jpeg").
};

/// A block representing a tool invocation requested by the assistant.
struct ToolUseBlock
{
    std::string id;           ///< Unique identifier for this tool call.
    std::string name;         ///< Name of the tool to invoke.
    nlohmann::json arguments; ///< Tool input arguments as JSON.
};

/// A block containing the result of a tool execution.
struct ToolResultBlock
{
    std::string toolUseId; ///< ID of the corresponding ToolUseBlock.
    std::string content;   ///< Tool output as text.
    bool isError = false;  ///< Whether the tool execution resulted in an error.
};

/// A variant representing any kind of content block in a message.
using ContentBlock = std::variant<TextBlock, ImageBlock, ToolUseBlock, ToolResultBlock>;

/// A single message in a chat conversation.
struct ChatMessage
{
    Role role;
    std::vector<ContentBlock> content;

    /// Returns the concatenation of all TextBlock contents in this message.
    [[nodiscard]] auto textContent() const -> std::string
    {
        auto result = std::string {};
        for (auto const& block: content)
        {
            if (auto const* text = std::get_if<TextBlock>(&block))
            {
                if (!result.empty())
                    result += '\n';
                result += text->text;
            }
        }
        return result;
    }

    /// Factory method for creating a simple text message.
    [[nodiscard]] static auto text(Role role, std::string text) -> ChatMessage
    {
        auto msg = ChatMessage { .role = role };
        msg.content.emplace_back(TextBlock { .text = std::move(text) });
        return msg;
    }
};

/// Normalized representation of a tool call extracted from a GenerateResult.
struct ToolCall
{
    std::string id;           ///< Unique identifier for this tool call.
    std::string name;         ///< Name of the tool to invoke.
    nlohmann::json arguments; ///< Tool input arguments as JSON.
};

/// Result of executing a tool, to be sent back to the model.
struct ToolResult
{
    std::string callId;   ///< ID of the corresponding ToolCall.
    std::string content;  ///< Tool output as text.
    bool isError = false; ///< Whether the tool execution resulted in an error.
};

/// A single recorded tool invocation trace entry.
struct ToolTraceEntry
{
    std::string timestamp;                    ///< ISO 8601 UTC timestamp.
    std::string callId;                       ///< Unique tool call identifier.
    std::string toolName;                     ///< Name of the invoked tool.
    nlohmann::json arguments;                 ///< Tool input arguments.
    std::string resultContent;                ///< Tool output content (post-truncation).
    bool resultIsError = false;               ///< Whether the tool returned an error.
    std::chrono::milliseconds duration { 0 }; ///< Execution duration.
};

/// Token usage statistics from an LLM API response.
struct TokenUsage
{
    int64_t inputTokens = 0;         ///< Prompt/input tokens consumed.
    int64_t outputTokens = 0;        ///< Completion/output tokens generated.
    int64_t cacheReadTokens = 0;     ///< Tokens served from prompt cache (cost reduction).
    int64_t cacheCreationTokens = 0; ///< Tokens written into prompt cache.

    /// Accumulates another usage record into this one.
    constexpr auto operator+=(TokenUsage const& other) noexcept -> TokenUsage&
    {
        inputTokens += other.inputTokens;
        outputTokens += other.outputTokens;
        cacheReadTokens += other.cacheReadTokens;
        cacheCreationTokens += other.cacheCreationTokens;
        return *this;
    }
};

/// Definition of a tool that the model can invoke.
struct ToolDefinition
{
    std::string name;           ///< Unique tool name.
    std::string description;    ///< Human-readable description of what the tool does.
    nlohmann::json inputSchema; ///< JSON Schema describing the tool's input parameters.
};

/// Result from a model generation request.
struct GenerateResult
{
    std::vector<ContentBlock> content; ///< Generated content blocks.
    std::vector<ToolCall> toolCalls;   ///< Tool calls extracted from the response.
    std::optional<TokenUsage> usage;   ///< Token usage statistics, if available.

    /// Returns true if the model requested any tool calls.
    [[nodiscard]] auto hasToolCalls() const noexcept -> bool { return !toolCalls.empty(); }

    /// Returns the concatenation of all TextBlock contents in the result.
    [[nodiscard]] auto textContent() const -> std::string
    {
        auto result = std::string {};
        for (auto const& block: content)
        {
            if (auto const* text = std::get_if<TextBlock>(&block))
            {
                if (!result.empty())
                    result += '\n';
                result += text->text;
            }
        }
        return result;
    }
};

/// Callback invoked for each text token as it is streamed from the model.
/// @return true to continue streaming, false to abort.
using StreamCallback = std::function<bool(std::string_view token)>;

/// Information about a model's capabilities.
struct ModelInfo
{
    std::string providerName;         ///< Provider identifier (e.g. "claude", "openai").
    std::string modelName;            ///< Model identifier (e.g. "claude-sonnet-4-5-20250929").
    size_t contextSize = 0;           ///< Maximum context window in tokens.
    bool supportsToolUse = false;     ///< Whether the model supports tool/function calling.
    bool supportsImageInput = false;  ///< Whether the model accepts images as input.
    bool supportsImageOutput = false; ///< Whether the model can generate images.
};

/// Thinking/reasoning mode for LLM providers that support adaptive or extended thinking.
///
/// Maps to provider-specific parameters:
/// - Claude: `thinking` field with `budget_tokens`
/// - OpenAI: `reasoning_effort` parameter
/// - Gemini: `thinkingConfig` with `thinkingBudget`
enum class ThinkingMode : uint8_t
{
    Off,      ///< No thinking parameters sent (provider default behavior).
    Normal,   ///< Moderate thinking budget / effort.
    Extended, ///< Maximum thinking budget / high reasoning effort.
};

/// Converts a ThinkingMode to its string representation (for config persistence).
[[nodiscard]] constexpr auto thinkingModeToString(ThinkingMode mode) noexcept -> std::string_view
{
    switch (mode)
    {
        case ThinkingMode::Off: return "off";
        case ThinkingMode::Normal: return "normal";
        case ThinkingMode::Extended: return "extended";
    }
    return "off";
}

/// Parses a string into a ThinkingMode enum.
/// @return The matching ThinkingMode, or ThinkingMode::Off if unrecognized.
[[nodiscard]] constexpr auto thinkingModeFromString(std::string_view str) noexcept -> ThinkingMode
{
    if (str == "normal")
        return ThinkingMode::Normal;
    if (str == "extended")
        return ThinkingMode::Extended;
    return ThinkingMode::Off;
}

/// Advances to the next thinking mode in the cycle: Off -> Normal -> Extended -> Off.
[[nodiscard]] constexpr auto nextThinkingMode(ThinkingMode mode) noexcept -> ThinkingMode
{
    switch (mode)
    {
        case ThinkingMode::Off: return ThinkingMode::Normal;
        case ThinkingMode::Normal: return ThinkingMode::Extended;
        case ThinkingMode::Extended: return ThinkingMode::Off;
    }
    return ThinkingMode::Off;
}

/// Error code categories for provider errors.
enum class ProviderErrorCode : uint8_t
{
    Unknown,
    NetworkError,
    AuthenticationError,
    RateLimitError,
    InvalidResponse,
    ServerError,
    ConfigError,
};

/// Error information from a provider operation.
struct ProviderError
{
    ProviderErrorCode code = ProviderErrorCode::Unknown;
    std::string message;
    int httpStatus = 0;
};

/// Estimates the cost of a turn based on token usage and model name.
/// @param usage        Token usage statistics.
/// @param providerName Provider identifier (e.g. "claude", "openai", "gemini").
/// @param modelName    Model identifier (e.g. "claude-sonnet-4-6").
/// @return Estimated cost in USD, or 0.0 if the model is unknown.
[[nodiscard]] auto estimateCost(TokenUsage const& usage,
                                std::string_view providerName,
                                std::string_view modelName) -> double;

/// Formats a token count for human-readable display.
/// @param count Token count to format.
/// @return Formatted string (e.g. "456", "1.2k", "12.3k", "1.2M").
[[nodiscard]] auto formatTokenCount(int64_t count) -> std::string;

} // namespace endo::agent
