// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <http/HttpClient.hpp>

#include <cstddef>
#include <expected>
#include <functional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include <agent/Types.hpp>
#include <agent/providers/LlmProvider.hpp>
#include <nlohmann/json.hpp>

namespace endo::agent
{

/// Callback type for refreshing an expired OAuth token.
/// Returns the new access token on success, or an error message.
using TokenRefresher = std::function<std::expected<std::string, std::string>()>;

/// Configuration for the Claude provider.
struct ClaudeProviderConfig
{
    std::string apiKey;                                ///< API key or OAuth access token.
    std::string model = "claude-sonnet-4-6";           ///< Model identifier.
    std::string baseUrl = "https://api.anthropic.com"; ///< Base URL for the API.
    std::string apiVersion = "2023-06-01";             ///< Anthropic API version header.
    size_t maxTokens = 8192;                           ///< Maximum output tokens per request.
    size_t contextWindowSize = 200000;                 ///< Maximum context window in tokens.
    ThinkingMode thinkingMode = ThinkingMode::Off;     ///< Thinking/reasoning mode.
    TokenRefresher tokenRefresher;                     ///< Optional: refreshes OAuth token on 401.
};

/// Represents a parsed SSE event from the Claude streaming API.
struct ClaudeSseResult
{
    /// Indicates whether streaming is complete.
    bool done = false;

    /// Text token received (empty if not a text delta).
    std::string textDelta;

    /// Completed content blocks (text or tool_use) finalized during this event.
    std::vector<ContentBlock> completedBlocks;

    /// Tool calls extracted when tool_use blocks are finalized.
    std::vector<ToolCall> completedToolCalls;

    /// Token usage statistics extracted from message_start or message_delta events.
    std::optional<TokenUsage> usage;
};

/// Tracks the state of an in-progress content block during SSE streaming.
struct ContentBlockAccumulator
{
    /// Type of the block: "text" or "tool_use".
    std::string type;

    /// Accumulated text (for text blocks).
    std::string text;

    /// Tool use ID (for tool_use blocks).
    std::string toolId;

    /// Tool name (for tool_use blocks).
    std::string toolName;

    /// Accumulated JSON string for tool arguments (for tool_use blocks).
    std::string toolArgumentsJson;
};

/// LLM provider implementation for the Anthropic Claude API.
///
/// Supports streaming responses via Server-Sent Events, tool use,
/// and image input (base64-encoded).
class ClaudeProvider final: public LlmProvider
{
  public:
    /// Constructs a ClaudeProvider.
    /// @param httpClient Reference to the HTTP client used for API requests.
    /// @param config     Provider configuration.
    ClaudeProvider(http::HttpClient const& httpClient, ClaudeProviderConfig config);

    /// Generates a response from the Claude model.
    [[nodiscard]] auto generate(std::span<ChatMessage const> messages,
                                std::span<ToolDefinition const> tools,
                                StreamCallback streamCb)
        -> std::expected<GenerateResult, ProviderError> override;

    /// Returns true; Claude supports tool/function calling.
    [[nodiscard]] auto supportsToolUse() const noexcept -> bool override;

    /// Returns true; Claude supports image input via base64.
    [[nodiscard]] auto supportsImageInput() const noexcept -> bool override;

    /// Returns false; Claude does not support image generation.
    [[nodiscard]] auto supportsImageOutput() const noexcept -> bool override;

    /// Returns the configured context window size.
    [[nodiscard]] auto contextSize() const noexcept -> size_t override;

    /// Returns detailed model information.
    [[nodiscard]] auto modelInfo() const -> ModelInfo override;

    /// Serializes a conversation and tools into a Claude API request body.
    /// @param messages     Conversation history.
    /// @param tools        Tool definitions available to the model.
    /// @param model        Model identifier string.
    /// @param maxTokens    Maximum output tokens.
    /// @param thinkingMode Thinking/reasoning mode to apply.
    /// @return JSON request body ready for the Claude Messages API.
    [[nodiscard]] static auto serializeRequest(std::span<ChatMessage const> messages,
                                               std::span<ToolDefinition const> tools,
                                               std::string const& model,
                                               size_t maxTokens,
                                               ThinkingMode thinkingMode) -> nlohmann::json;

    /// Parses a single SSE event from the Claude streaming API.
    /// @param event        The SSE event to parse.
    /// @param accumulator  In-progress content block state, indexed by block index.
    /// @return Parsed result containing deltas, completed blocks, and tool calls.
    [[nodiscard]] static auto parseSseEvent(http::SseEvent const& event,
                                            std::vector<ContentBlockAccumulator>& accumulator)
        -> std::expected<ClaudeSseResult, ProviderError>;

  private:
    /// Builds an HTTP request with appropriate auth headers (OAuth vs API key).
    [[nodiscard]] auto buildRequest(std::span<ChatMessage const> messages,
                                    std::span<ToolDefinition const> tools) const -> http::HttpRequest;

    /// Executes a streaming request and collects the result.
    [[nodiscard]] auto executeStreaming(http::HttpRequest const& request, StreamCallback const& streamCb)
        -> std::expected<GenerateResult, ProviderError>;

    /// Maps an HTTP status code to a ProviderError.
    [[nodiscard]] static auto mapHttpError(long statusCode, std::string message) -> ProviderError;

    http::HttpClient const& _httpClient;
    ClaudeProviderConfig _config;
};

} // namespace endo::agent
