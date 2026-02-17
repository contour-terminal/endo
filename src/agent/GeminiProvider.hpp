// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <span>
#include <string>

#include <agent/LlmProvider.hpp>
#include <agent/Types.hpp>
#include <nlohmann/json.hpp>

namespace endo::http
{
class HttpClient;
} // namespace endo::http

namespace endo::agent
{

/// Configuration for the Google Gemini provider.
struct GeminiProviderConfig
{
    std::string apiKey;                     ///< API key for authentication.
    std::string model = "gemini-2.5-flash"; ///< Model identifier.
    size_t maxTokens = 8192;                ///< Maximum output tokens per request.
    size_t contextWindowSize = 1000000;     ///< Maximum context window in tokens.
};

/// LLM provider implementation for Google Gemini API.
///
/// Supports streaming generation, tool use, image input, and image output
/// via the Gemini REST API with Server-Sent Events.
class GeminiProvider final: public LlmProvider
{
  public:
    /// Constructs a GeminiProvider.
    /// @param httpClient HTTP client used for API requests.
    /// @param config     Provider configuration including API key and model.
    GeminiProvider(http::HttpClient const& httpClient, GeminiProviderConfig config);

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

    /// Serializes messages and tools into the Gemini API request JSON format.
    /// @param messages  Conversation history to serialize.
    /// @param tools     Tool definitions to include in the request.
    /// @param maxTokens Maximum output tokens for generation config.
    /// @return The serialized JSON request body.
    [[nodiscard]] static auto serializeRequest(std::span<ChatMessage const> messages,
                                               std::span<ToolDefinition const> tools,
                                               size_t maxTokens) -> nlohmann::json;

  private:
    http::HttpClient const& _httpClient;
    GeminiProviderConfig _config;

    /// Builds the full API endpoint URL including the API key.
    [[nodiscard]] auto buildUrl() const -> std::string;

    /// Maps an HTTP status code to the appropriate ProviderErrorCode.
    /// @param statusCode The HTTP response status code.
    /// @return The corresponding error code.
    [[nodiscard]] static auto mapHttpError(long statusCode) -> ProviderErrorCode;

    /// Finds the tool name for a given tool use ID by searching backwards through messages.
    /// @param messages  The conversation history to search.
    /// @param toolUseId The tool use ID to look up.
    /// @return The tool name, or the toolUseId as fallback if not found.
    [[nodiscard]] static auto findToolName(std::span<ChatMessage const> messages, std::string_view toolUseId)
        -> std::string;
};

} // namespace endo::agent
