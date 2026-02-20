// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <http/HttpClient.hpp>

#include <span>
#include <string>

#include <agent/Types.hpp>
#include <agent/providers/LlmProvider.hpp>
#include <nlohmann/json.hpp>

namespace endo::agent
{

/// Configuration for the OpenAI provider.
struct OpenAiProviderConfig
{
    std::string apiKey;                                ///< API key (can be empty for local models).
    std::string model = "gpt-4o";                      ///< Model identifier.
    std::string baseUrl = "https://api.openai.com/v1"; ///< Base URL for the API.
    size_t maxTokens = 4096;                           ///< Maximum output tokens per request.
    size_t contextWindowSize = 128000;                 ///< Context window size in tokens.
    bool supportsImages = true;                        ///< Whether the model accepts image inputs.
    bool supportsTools = true;                         ///< Whether the model supports tool calling.
    ThinkingMode thinkingMode = ThinkingMode::Off; ///< Thinking/reasoning mode (maps to reasoning_effort).
};

/// LLM provider implementation for the OpenAI Chat Completions API.
///
/// Supports streaming responses via Server-Sent Events, tool calling,
/// and multimodal (text + image) inputs. Compatible with any OpenAI-compatible
/// API endpoint (e.g., local models via baseUrl override).
class OpenAiProvider final: public LlmProvider
{
  public:
    /// Constructs an OpenAI provider.
    /// @param httpClient  HTTP client for making API requests.
    /// @param config      Provider configuration.
    OpenAiProvider(http::HttpClient const& httpClient, OpenAiProviderConfig config);

    /// @copydoc LlmProvider::generate
    [[nodiscard]] auto generate(std::span<ChatMessage const> messages,
                                std::span<ToolDefinition const> tools,
                                StreamCallback streamCb)
        -> std::expected<GenerateResult, ProviderError> override;

    /// @copydoc LlmProvider::supportsToolUse
    [[nodiscard]] auto supportsToolUse() const noexcept -> bool override;

    /// @copydoc LlmProvider::supportsImageInput
    [[nodiscard]] auto supportsImageInput() const noexcept -> bool override;

    /// @copydoc LlmProvider::supportsImageOutput
    [[nodiscard]] auto supportsImageOutput() const noexcept -> bool override;

    /// @copydoc LlmProvider::contextSize
    [[nodiscard]] auto contextSize() const noexcept -> size_t override;

    /// @copydoc LlmProvider::modelInfo
    [[nodiscard]] auto modelInfo() const -> ModelInfo override;

    /// Serializes messages and tools into an OpenAI Chat Completions request body.
    /// @param messages     Conversation messages to include.
    /// @param tools        Tool definitions available to the model.
    /// @param model        Model identifier string.
    /// @param maxTokens    Maximum output tokens.
    /// @param thinkingMode Thinking/reasoning mode (maps to reasoning_effort).
    /// @return JSON object suitable for POST to /chat/completions.
    [[nodiscard]] static auto serializeRequest(std::span<ChatMessage const> messages,
                                               std::span<ToolDefinition const> tools,
                                               std::string const& model,
                                               size_t maxTokens,
                                               ThinkingMode thinkingMode) -> nlohmann::json;

    /// Parses an SSE data payload from the OpenAI streaming response.
    /// @param data  The JSON string from an SSE data line.
    /// @return Parsed JSON object, or std::nullopt for the [DONE] sentinel.
    [[nodiscard]] static auto parseSseData(std::string_view data) -> std::optional<nlohmann::json>;

  private:
    /// Maps an HTTP status code to a ProviderError.
    [[nodiscard]] static auto mapHttpError(long statusCode, std::string const& body) -> ProviderError;

    http::HttpClient const& _httpClient;
    OpenAiProviderConfig _config;
};

} // namespace endo::agent
