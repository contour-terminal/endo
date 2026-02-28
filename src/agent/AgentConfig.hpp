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
    bool promptCaching = true;                     ///< Enable prompt caching via cache_control.
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
    std::string authPreference = "auto";           ///< Auth method: "auto", "oauth", "api_key".
    ThinkingMode thinkingMode = ThinkingMode::Off; ///< Thinking/reasoning mode.
};

/// Configuration for the GitHub Copilot provider.
struct CopilotConfig
{
    std::string model = "gpt-4o";                  ///< Model identifier.
    size_t maxTokens = 4096;                       ///< Maximum output tokens per request.
    ThinkingMode thinkingMode = ThinkingMode::Off; ///< Thinking/reasoning mode.
};

/// Configuration for the local llama.cpp inference provider.
struct LocalConfig
{
    std::string modelPath;                                         ///< Path to GGUF model file.
    std::string modelDir = "~/.local/share/endo/models/";          ///< Directory for model storage.
    int32_t gpuLayers = -1;                                        ///< GPU layers to offload (-1 = all, 0 = CPU only).
    size_t contextSize = 32768;                                    ///< Context window size in tokens.
    int32_t threads = 0;                                           ///< Number of threads (0 = auto-detect).
    size_t batchSize = 512;                                        ///< Batch size for prompt evaluation.
    float temperature = 0.7f;                                      ///< Sampling temperature.
    float topP = 0.9f;                                             ///< Top-p (nucleus) sampling.
    int32_t topK = 40;                                             ///< Top-k sampling.
    float repeatPenalty = 1.1f;                                    ///< Repetition penalty.
    bool flashAttention = true;                                    ///< Enable flash attention if supported.
    size_t maxTokens = 4096;                                       ///< Maximum output tokens per request.
    std::string chatTemplate;                                      ///< Chat template override (empty = auto-detect).
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

/// @brief Default action for error recovery on failed shell commands.
enum class ErrorRecoveryAction : std::uint8_t
{
    Ask,     ///< Ask the user via QuestionComponent (default).
    Analyze, ///< Automatically analyze without asking.
    Ignore,  ///< Do nothing on command failure.
};

/// @brief Converts an ErrorRecoveryAction to its string representation.
/// @param action The action to convert.
/// @return String view of the action name ("ask", "analyze", or "ignore").
[[nodiscard]] constexpr auto errorRecoveryActionToString(ErrorRecoveryAction action) -> std::string_view
{
    switch (action)
    {
        case ErrorRecoveryAction::Ask: return "ask";
        case ErrorRecoveryAction::Analyze: return "analyze";
        case ErrorRecoveryAction::Ignore: return "ignore";
    }
    return "ask";
}

/// @brief Parses an ErrorRecoveryAction from a string.
/// @param str The string to parse ("ask", "analyze", or "ignore").
/// @return The corresponding action (defaults to Ask for unknown strings).
[[nodiscard]] constexpr auto errorRecoveryActionFromString(std::string_view str) -> ErrorRecoveryAction
{
    if (str == "analyze")
        return ErrorRecoveryAction::Analyze;
    if (str == "ignore")
        return ErrorRecoveryAction::Ignore;
    return ErrorRecoveryAction::Ask;
}

/// @brief Configuration for error recovery suggestions on failed shell commands.
struct ErrorRecoveryConfig
{
    ErrorRecoveryAction action = ErrorRecoveryAction::Ask; ///< Default action on command failure.
    std::string model; ///< Model to use for error analysis (empty = use active agent model).
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
    CopilotConfig copilot;     ///< GitHub Copilot configuration.
    LocalConfig local;         ///< Local llama.cpp inference configuration.

    size_t maxToolResultSize = 30720; ///< Maximum size in bytes for tool result content before truncation.
    bool logToolUses = true;          ///< Whether to log tool invocations to the terminal in agent mode.

    PlanModeConfig planMode;           ///< Plan mode configuration.
    ExploreConfig explore;             ///< Explore sub-agent configuration.
    TraceConfig trace;                 ///< Tool I/O tracing configuration.
    SessionConfig session;             ///< Session management configuration.
    PermissionConfig permissions;      ///< Tool permission configuration.
    ErrorRecoveryConfig errorRecovery; ///< Error recovery suggestions configuration.
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
