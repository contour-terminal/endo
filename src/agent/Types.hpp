// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
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
using StreamCallback = std::function<void(std::string_view token)>;

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

} // namespace endo::agent
