// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <span>
#include <string>
#include <vector>

#include <agent/Types.hpp>
#include <agent/providers/LlmProvider.hpp>
#include <nlohmann/json.hpp>

namespace endo::http
{
class HttpClient;
struct HttpRequest;
} // namespace endo::http

namespace endo::agent
{

/// Configuration for the Google Gemini provider.
struct GeminiProviderConfig
{
    std::string apiKey;                            ///< API key or OAuth access token.
    std::string model = "gemini-2.5-flash";        ///< Model identifier.
    size_t maxTokens = 8192;                       ///< Maximum output tokens per request.
    size_t contextWindowSize = 1000000;            ///< Maximum context window in tokens.
    ThinkingMode thinkingMode = ThinkingMode::Off; ///< Thinking/reasoning mode.
    bool useOAuth = false;                         ///< Whether to use OAuth Bearer auth instead of API key.
    TokenRefresher tokenRefresher;                 ///< Optional: refreshes OAuth token on 401.
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
    /// @param messages     Conversation history to serialize.
    /// @param tools        Tool definitions to include in the request.
    /// @param maxTokens    Maximum output tokens for generation config.
    /// @param thinkingMode Thinking/reasoning mode to apply.
    /// @return The serialized JSON request body.
    [[nodiscard]] static auto serializeRequest(std::span<ChatMessage const> messages,
                                               std::span<ToolDefinition const> tools,
                                               size_t maxTokens,
                                               ThinkingMode thinkingMode) -> nlohmann::json;

    /// Wraps a standard Gemini request body for the Code Assist API by adding the model field.
    /// @param innerRequest The standard Gemini API request JSON.
    /// @return The request with model added as a top-level sibling of contents/generationConfig.
    [[nodiscard]] auto wrapCodeAssistRequest(nlohmann::json innerRequest) const -> nlohmann::json;

  private:
    http::HttpClient const& _httpClient;
    GeminiProviderConfig _config;
    bool _codeAssistOnboarded = false; ///< Whether Code Assist onboarding has been verified.

    /// Executes a streaming request and collects the result.
    [[nodiscard]] auto executeStreaming(http::HttpRequest const& request, StreamCallback const& streamCb)
        -> std::expected<GenerateResult, ProviderError>;

    /// Builds the full API endpoint URL (with or without API key depending on auth mode).
    [[nodiscard]] auto buildUrl() const -> std::string;

    /// Builds HTTP headers with appropriate authentication (Bearer for OAuth, Content-Type only for API key).
    [[nodiscard]] auto buildAuthHeaders() const -> std::vector<std::string>;

    /// Ensures Code Assist onboarding is complete (lazy, one-shot per provider lifetime).
    /// @return std::nullopt on success, or a ProviderError if onboarding fails.
    [[nodiscard]] auto ensureCodeAssistOnboarded() -> std::optional<ProviderError>;

    /// Maps an HTTP status code and response body to a ProviderError.
    /// @param statusCode The HTTP response status code.
    /// @param body       The raw HTTP response body (may contain JSON error details).
    /// @return The corresponding provider error with extracted message.
    [[nodiscard]] static auto mapHttpError(long statusCode, std::string const& body) -> ProviderError;

    /// Finds the tool name for a given tool use ID by searching backwards through messages.
    /// @param messages  The conversation history to search.
    /// @param toolUseId The tool use ID to look up.
    /// @return The tool name, or the toolUseId as fallback if not found.
    [[nodiscard]] static auto findToolName(std::span<ChatMessage const> messages, std::string_view toolUseId)
        -> std::string;
};

} // namespace endo::agent
