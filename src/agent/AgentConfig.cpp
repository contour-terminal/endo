// SPDX-License-Identifier: Apache-2.0
#include "AgentConfig.hpp"

#include <yaml-cpp/yaml.h>

#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>

#include <platform/UserPaths.hpp>

namespace endo::agent
{

namespace
{
    void parseClaudeConfig(YAML::Node const& node, ClaudeConfig& config)
    {
        if (!node || !node.IsMap())
            return;
        if (node["api_key"])
            config.apiKey = node["api_key"].as<std::string>();
        if (node["api_key_env"])
            config.apiKeyEnv = node["api_key_env"].as<std::string>();
        if (node["model"])
            config.model = node["model"].as<std::string>();
        if (node["max_tokens"])
            config.maxTokens = node["max_tokens"].as<size_t>();
        if (node["auth_preference"])
            config.authPreference = node["auth_preference"].as<std::string>();
        if (node["thinking_mode"])
            config.thinkingMode = thinkingModeFromString(node["thinking_mode"].as<std::string>());
        if (node["prompt_caching"])
            config.promptCaching = node["prompt_caching"].as<bool>();
    }

    void parseOpenAiConfig(YAML::Node const& node, OpenAiConfig& config)
    {
        if (!node || !node.IsMap())
            return;
        if (node["api_key"])
            config.apiKey = node["api_key"].as<std::string>();
        if (node["api_key_env"])
            config.apiKeyEnv = node["api_key_env"].as<std::string>();
        if (node["model"])
            config.model = node["model"].as<std::string>();
        if (node["base_url"])
            config.baseUrl = node["base_url"].as<std::string>();
        if (node["max_tokens"])
            config.maxTokens = node["max_tokens"].as<size_t>();
        if (node["thinking_mode"])
            config.thinkingMode = thinkingModeFromString(node["thinking_mode"].as<std::string>());
    }

    void parseGeminiConfig(YAML::Node const& node, GeminiConfig& config)
    {
        if (!node || !node.IsMap())
            return;
        if (node["api_key"])
            config.apiKey = node["api_key"].as<std::string>();
        if (node["api_key_env"])
            config.apiKeyEnv = node["api_key_env"].as<std::string>();
        if (node["model"])
            config.model = node["model"].as<std::string>();
        if (node["max_tokens"])
            config.maxTokens = node["max_tokens"].as<size_t>();
        if (node["auth_preference"])
            config.authPreference = node["auth_preference"].as<std::string>();
        if (node["thinking_mode"])
            config.thinkingMode = thinkingModeFromString(node["thinking_mode"].as<std::string>());
    }

    void parseCopilotConfig(YAML::Node const& node, CopilotConfig& config)
    {
        if (!node || !node.IsMap())
            return;
        if (node["model"])
            config.model = node["model"].as<std::string>();
        if (node["max_tokens"])
            config.maxTokens = node["max_tokens"].as<size_t>();
        if (node["thinking_mode"])
            config.thinkingMode = thinkingModeFromString(node["thinking_mode"].as<std::string>());
    }

    void parseLocalConfig(YAML::Node const& node, LocalConfig& config)
    {
        if (!node || !node.IsMap())
            return;
        if (node["model_path"])
            config.modelPath = node["model_path"].as<std::string>();
        if (node["model_dir"])
            config.modelDir = node["model_dir"].as<std::string>();
        if (node["gpu_layers"])
            config.gpuLayers = node["gpu_layers"].as<int32_t>();
        if (node["context_size"])
            config.contextSize = node["context_size"].as<size_t>();
        if (node["threads"])
            config.threads = node["threads"].as<int32_t>();
        if (node["batch_size"])
            config.batchSize = node["batch_size"].as<size_t>();
        if (node["temperature"])
            config.temperature = node["temperature"].as<float>();
        if (node["top_p"])
            config.topP = node["top_p"].as<float>();
        if (node["top_k"])
            config.topK = node["top_k"].as<int32_t>();
        if (node["repeat_penalty"])
            config.repeatPenalty = node["repeat_penalty"].as<float>();
        if (node["flash_attention"])
            config.flashAttention = node["flash_attention"].as<bool>();
        if (node["max_tokens"])
            config.maxTokens = node["max_tokens"].as<size_t>();
        if (node["chat_template"])
            config.chatTemplate = node["chat_template"].as<std::string>();
    }

    void parsePlanModeConfig(YAML::Node const& node, PlanModeConfig& config)
    {
        if (!node || !node.IsMap())
            return;
        if (node["enabled"])
            config.enabled = node["enabled"].as<bool>();
        if (node["pause_between_steps"])
            config.pauseBetweenSteps = node["pause_between_steps"].as<bool>();
        if (node["max_exploration_turns"])
            config.maxExplorationTurns = node["max_exploration_turns"].as<size_t>();
    }

    void parseExploreConfig(YAML::Node const& node, ExploreConfig& config)
    {
        if (!node || !node.IsMap())
            return;
        if (node["max_turns"])
            config.maxTurns = node["max_turns"].as<size_t>();
    }

    void parseTraceConfig(YAML::Node const& node, TraceConfig& config)
    {
        if (!node || !node.IsMap())
            return;
        if (node["enabled"])
            config.enabled = node["enabled"].as<bool>();
        if (node["terminal"])
            config.terminal = node["terminal"].as<bool>();
        if (node["default_path"])
            config.defaultPath = node["default_path"].as<std::string>();
        if (node["max_files"])
            config.maxFiles = node["max_files"].as<size_t>();
    }

    /// @brief Emits a provider section containing only api_key and api_key_env.
    ///
    /// @param emitter       YAML emitter to write to.
    /// @param sectionName   Provider section name (e.g. "claude", "openai").
    /// @param apiKey        Stored API key (empty = not set).
    /// @param apiKeyEnv     Environment variable name for API key lookup.
    /// @param defaultApiKeyEnv  Default environment variable name (to skip if unchanged).
    void emitProviderKeys(YAML::Emitter& emitter,
                          std::string_view sectionName,
                          std::string const& apiKey,
                          std::string const& apiKeyEnv,
                          std::string const& defaultApiKeyEnv)
    {
        auto const hasApiKey = !apiKey.empty();
        auto const hasCustomApiKeyEnv = apiKeyEnv != defaultApiKeyEnv;

        if (!hasApiKey && !hasCustomApiKeyEnv)
            return;

        emitter << YAML::Key << std::string(sectionName) << YAML::Value << YAML::BeginMap;
        if (hasApiKey)
            emitter << YAML::Key << "api_key" << YAML::Value << apiKey;
        if (hasCustomApiKeyEnv)
            emitter << YAML::Key << "api_key_env" << YAML::Value << apiKeyEnv;
        emitter << YAML::EndMap;
    }
} // namespace

auto loadAgentConfig(std::filesystem::path const& path) -> std::expected<AgentConfig, std::string>
{
    if (!std::filesystem::exists(path))
        return std::unexpected(std::format("Config file not found: {}", path.string()));

    try
    {
        auto const root = YAML::LoadFile(path.string());
        auto config = AgentConfig {};

        if (root["active_provider"])
            config.activeProvider = root["active_provider"].as<std::string>();
        if (root["prompt_indicator"])
            config.promptIndicator = root["prompt_indicator"].as<std::string>();

        parseClaudeConfig(root["claude"], config.claude);
        parseOpenAiConfig(root["openai"], config.openai);
        parseOpenAiConfig(root["openai_compat"], config.openaiCompat);
        parseGeminiConfig(root["gemini"], config.gemini);
        parseCopilotConfig(root["copilot"], config.copilot);
        parseLocalConfig(root["local"], config.local);

        if (root["max_tool_result_size"])
            config.maxToolResultSize = root["max_tool_result_size"].as<size_t>();
        if (root["log_tool_uses"])
            config.logToolUses = root["log_tool_uses"].as<bool>();

        parsePlanModeConfig(root["plan_mode"], config.planMode);
        parseExploreConfig(root["explore"], config.explore);
        parseTraceConfig(root["trace"], config.trace);

        return config;
    }
    catch (YAML::Exception const& e)
    {
        return std::unexpected(std::string(e.what()));
    }
    catch (std::exception const& e)
    {
        return std::unexpected(std::string(e.what()));
    }
    catch (...)
    {
        return std::unexpected(std::format("Failed to load config: {}", path.string()));
    }
}

auto loadAgentConfig() -> AgentConfig
{
    auto const configDir = platform::configHome();
    if (!configDir)
        return AgentConfig {};

    auto const path = *configDir / "endo" / "agent.yml";
    if (!std::filesystem::exists(path))
        return AgentConfig {};

    auto result = loadAgentConfig(path);
    if (result.has_value())
        return std::move(*result);

    return AgentConfig {};
}

auto resolveApiKey(std::string_view envVarName) -> std::optional<std::string>
{
    auto const* value = std::getenv(std::string(envVarName).c_str());
    if (!value || *value == '\0')
        return std::nullopt;
    return std::string(value);
}

auto resolveProviderApiKey(std::string const& apiKey, std::string const& apiKeyEnv)
    -> std::optional<std::string>
{
    if (!apiKey.empty())
        return apiKey;
    return resolveApiKey(apiKeyEnv);
}

auto saveAgentConfig(AgentConfig const& config, std::filesystem::path const& path)
    -> std::optional<std::string>
{
    try
    {
        (void) std::filesystem::create_directories(path.parent_path());

        auto emitter = YAML::Emitter {};
        emitter << YAML::BeginMap;

        auto const defaults = AgentConfig {};

        // Only persist API keys and custom api_key_env — all other settings
        // belong in init.endo and are session-only at runtime.
        emitProviderKeys(
            emitter, "claude", config.claude.apiKey, config.claude.apiKeyEnv, defaults.claude.apiKeyEnv);
        emitProviderKeys(
            emitter, "openai", config.openai.apiKey, config.openai.apiKeyEnv, defaults.openai.apiKeyEnv);
        emitProviderKeys(emitter,
                         "openai_compat",
                         config.openaiCompat.apiKey,
                         config.openaiCompat.apiKeyEnv,
                         defaults.openaiCompat.apiKeyEnv);
        emitProviderKeys(
            emitter, "gemini", config.gemini.apiKey, config.gemini.apiKeyEnv, defaults.gemini.apiKeyEnv);

        emitter << YAML::EndMap;

        // Atomic write: write to .tmp, then rename
        auto const tmpPath = std::filesystem::path(path.string() + ".tmp");
        {
            auto ofs = std::ofstream(tmpPath);
            if (!ofs)
                return std::string("Failed to open temporary file for writing: ") + tmpPath.string();
            ofs << emitter.c_str() << '\n';
        }

        std::filesystem::rename(tmpPath, path);

        // Set restrictive permissions (owner-only read/write) since the file contains API keys.
#if !defined(_WIN32)
        std::filesystem::permissions(path,
                                     std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                     std::filesystem::perm_options::replace);
#endif

        return std::nullopt;
    }
    catch (std::exception const& e)
    {
        return std::string(e.what());
    }
}

auto saveAgentConfig(AgentConfig const& config) -> std::optional<std::string>
{
    auto const configDir = platform::configHome();
    if (!configDir)
        return std::string("User config directory not available");

    auto const path = *configDir / "endo" / "agent.yml";
    return saveAgentConfig(config, path);
}

} // namespace endo::agent
