// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <span>
#include <string>

#include <agent/Types.hpp>
#include <agent/auth/CopilotDeviceFlow.hpp>
#include <agent/providers/LlmProvider.hpp>

namespace endo::http
{
class HttpClient;
}

namespace endo::agent
{

/// Configuration for the GitHub Copilot provider.
struct CopilotProviderConfig
{
    std::string githubToken;                       ///< Long-lived GitHub OAuth token (ghu_ prefix).
    std::string model = "gpt-4o";                  ///< Model identifier.
    size_t maxTokens = 4096;                       ///< Maximum output tokens per request.
    size_t contextWindowSize = 128000;             ///< Context window size in tokens.
    ThinkingMode thinkingMode = ThinkingMode::Off; ///< Thinking/reasoning mode.
    TokenRefresher tokenRefresher;                 ///< Optional: reloads GitHub token from OAuthStore.
};

/// LLM provider for GitHub Copilot's OpenAI-compatible Chat Completions API.
///
/// Uses a two-tier token system: a long-lived GitHub OAuth token (stored on disk)
/// is exchanged for short-lived (~25 min) Copilot session tokens cached in memory.
/// Request serialization and SSE response parsing are delegated to OpenAiProvider's
/// static methods since the API format is identical.
class CopilotProvider final: public LlmProvider
{
  public:
    /// Constructs a CopilotProvider.
    /// @param httpClient HTTP client for making API requests.
    /// @param config     Provider configuration including GitHub token and model.
    CopilotProvider(http::HttpClient const& httpClient, CopilotProviderConfig config);

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

  private:
    /// Ensures the cached Copilot session token is valid, re-exchanging from the
    /// GitHub token if expired.
    /// @return The valid session token string, or a ProviderError.
    [[nodiscard]] auto ensureSessionToken() -> std::expected<std::string, ProviderError>;

    /// Maps an HTTP status code to a ProviderError.
    [[nodiscard]] static auto mapHttpError(long statusCode, std::string const& body) -> ProviderError;

    http::HttpClient const& _httpClient;
    CopilotProviderConfig _config;
    CopilotSessionToken _sessionToken; ///< Cached Copilot session token (in-memory only).
};

} // namespace endo::agent
