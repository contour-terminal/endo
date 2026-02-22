// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>

#include <agent/PermissionManager.hpp>
#include <agent/Types.hpp>

namespace endo::agent
{

/// Configuration for the Anthropic Claude provider.
struct ClaudeConfig
{
    std::string apiKey;                            ///< Stored API key (from config file).
    std::string apiKeyEnv = "ANTHROPIC_API_KEY";   ///< Environment variable holding the API key.
    std::string model = "claude-sonnet-4-6";       ///< Model identifier.
    size_t maxTokens = 8192;                       ///< Maximum output tokens per request.
    std::string authPreference = "auto";           ///< Auth method: "auto", "oauth", "api_key".
    ThinkingMode thinkingMode = ThinkingMode::Off; ///< Thinking/reasoning mode.
};

/// Configuration for OpenAI-compatible providers.
struct OpenAiConfig
{
    std::string apiKey;                            ///< Stored API key (from config file).
    std::string apiKeyEnv = "OPENAI_API_KEY";      ///< Environment variable holding the API key.
    std::string model = "gpt-4o";                  ///< Model identifier.
    std::string baseUrl;                           ///< Base URL (empty = https://api.openai.com/v1).
    size_t maxTokens = 4096;                       ///< Maximum output tokens per request.
    ThinkingMode thinkingMode = ThinkingMode::Off; ///< Thinking/reasoning mode.
};

/// Configuration for the Google Gemini provider.
struct GeminiConfig
{
    std::string apiKey;                            ///< Stored API key (from config file).
    std::string apiKeyEnv = "GEMINI_API_KEY";      ///< Environment variable holding the API key.
    std::string model = "gemini-2.5-flash";        ///< Model identifier.
    size_t maxTokens = 8192;                       ///< Maximum output tokens per request.
    ThinkingMode thinkingMode = ThinkingMode::Off; ///< Thinking/reasoning mode.
};

/// Configuration for agent plan mode.
struct PlanModeConfig
{
    bool enabled = true;             ///< Whether plan mode (/plan) is available.
    bool pauseBetweenSteps = false;  ///< Whether to pause for confirmation between steps.
    size_t maxExplorationTurns = 15; ///< Maximum exploration iterations before requiring a plan.
};

/// Configuration for the explore sub-agent tool.
struct ExploreConfig
{
    size_t maxTurns = 10; ///< Maximum exploration iterations for the sub-agent.
};

/// Configuration for agent tool I/O tracing.
struct TraceConfig
{
    bool enabled = false;    ///< Whether tracing is enabled by default.
    std::string defaultPath; ///< Default trace file path (empty = auto-generate).
    size_t maxFiles = 20;    ///< Maximum number of trace files to retain (oldest pruned first).
};

/// Configuration for agent session management.
struct SessionConfig
{
    bool autoResume = false;       ///< Whether to auto-resume the last named session on agent mode entry.
    bool showResumeContext = true; ///< Whether to show a summary message when resuming a session.
};

/// Top-level agent configuration supporting multiple LLM providers.
struct AgentConfig
{
    std::string activeProvider; ///< Which provider to use (empty = auto-detect from authenticated).
    std::string promptIndicator = "\xe2\x9d\xaf"; ///< Agent prompt indicator (default: ❯ U+276F).

    ClaudeConfig claude;       ///< Anthropic Claude configuration.
    OpenAiConfig openai;       ///< OpenAI configuration.
    OpenAiConfig openaiCompat; ///< OpenAI-compatible provider (Ollama, vLLM, LM Studio).
    GeminiConfig gemini;       ///< Google Gemini configuration.

    size_t maxToolResultSize = 30720; ///< Maximum size in bytes for tool result content before truncation.
    bool logToolUses = true;          ///< Whether to log tool invocations to the terminal in agent mode.

    PlanModeConfig planMode;      ///< Plan mode configuration.
    ExploreConfig explore;        ///< Explore sub-agent configuration.
    TraceConfig trace;            ///< Tool I/O tracing configuration.
    SessionConfig session;        ///< Session management configuration.
    PermissionConfig permissions; ///< Tool permission configuration.
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

/// Resolves the API key for a provider: stored key takes priority, then env var fallback.
/// @param apiKey Stored API key from config file.
/// @param apiKeyEnv Environment variable name for fallback.
/// @return The resolved API key, or std::nullopt if neither source is available.
[[nodiscard]] auto resolveProviderApiKey(std::string const& apiKey, std::string const& apiKeyEnv)
    -> std::optional<std::string>;

/// Saves agent configuration to a YAML file using atomic write (write to .tmp, then rename).
/// @param config The configuration to save.
/// @param path Target file path.
/// @return std::nullopt on success, or an error message.
[[nodiscard]] auto saveAgentConfig(AgentConfig const& config, std::filesystem::path const& path)
    -> std::optional<std::string>;

/// Saves agent configuration to the default path (~/.config/endo/agent.yml).
/// @param config The configuration to save.
/// @return std::nullopt on success, or an error message.
[[nodiscard]] auto saveAgentConfig(AgentConfig const& config) -> std::optional<std::string>;

} // namespace endo::agent
