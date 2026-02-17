// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>

namespace endo::agent
{

/// Configuration for the Anthropic Claude provider.
struct ClaudeConfig
{
    std::string apiKeyEnv = "ANTHROPIC_API_KEY";      ///< Environment variable holding the API key.
    std::string model = "claude-sonnet-4-5-20250929"; ///< Model identifier.
    size_t maxTokens = 8192;                          ///< Maximum output tokens per request.
};

/// Configuration for OpenAI-compatible providers.
struct OpenAiConfig
{
    std::string apiKeyEnv = "OPENAI_API_KEY"; ///< Environment variable holding the API key.
    std::string model = "gpt-4o";             ///< Model identifier.
    std::string baseUrl;                      ///< Base URL (empty = https://api.openai.com/v1).
    size_t maxTokens = 4096;                  ///< Maximum output tokens per request.
};

/// Configuration for the Google Gemini provider.
struct GeminiConfig
{
    std::string apiKeyEnv = "GEMINI_API_KEY"; ///< Environment variable holding the API key.
    std::string model = "gemini-2.5-flash";   ///< Model identifier.
    size_t maxTokens = 8192;                  ///< Maximum output tokens per request.
};

/// Top-level agent configuration supporting multiple LLM providers.
struct AgentConfig
{
    std::string activeProvider = "claude"; ///< Which provider to use by default.

    ClaudeConfig claude;       ///< Anthropic Claude configuration.
    OpenAiConfig openai;       ///< OpenAI configuration.
    OpenAiConfig openaiCompat; ///< OpenAI-compatible provider (Ollama, vLLM, LM Studio).
    GeminiConfig gemini;       ///< Google Gemini configuration.

    size_t maxToolResultSize = 30720; ///< Maximum size in bytes for tool result content before truncation.
};

/// Loads agent configuration from a YAML file.
/// @param path Path to the YAML configuration file.
/// @return The parsed configuration, or an error message.
[[nodiscard]] auto loadAgentConfig(std::filesystem::path const& path)
    -> std::expected<AgentConfig, std::string>;

/// Loads agent configuration from the default path (~/.config/endo/agent.yml).
/// Returns default configuration if the file does not exist.
[[nodiscard]] auto loadAgentConfig() -> AgentConfig;

/// Resolves an API key from an environment variable.
/// @param envVarName Name of the environment variable.
/// @return The API key value, or std::nullopt if not set.
[[nodiscard]] auto resolveApiKey(std::string_view envVarName) -> std::optional<std::string>;

} // namespace endo::agent
