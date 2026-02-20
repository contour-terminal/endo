// SPDX-License-Identifier: Apache-2.0
#include "AgentConfig.hpp"

#include <yaml-cpp/yaml.h>

#include <cstdlib>
#include <fstream>

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
        if (node["thinking_mode"])
            config.thinkingMode = thinkingModeFromString(node["thinking_mode"].as<std::string>());
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
        if (node["default_path"])
            config.defaultPath = node["default_path"].as<std::string>();
    }

    /// Emits a provider section with API keys, model, and thinking mode preferences.
    void emitProviderSection(YAML::Emitter& emitter,
                             std::string_view sectionName,
                             std::string const& apiKey,
                             std::string const& apiKeyEnv,
                             std::string const& defaultApiKeyEnv,
                             std::string const& model,
                             std::string const& defaultModel,
                             ThinkingMode thinkingMode)
    {
        bool const hasApiKey = !apiKey.empty();
        bool const hasCustomApiKeyEnv = apiKeyEnv != defaultApiKeyEnv;
        bool const hasCustomModel = model != defaultModel;
        bool const hasThinking = thinkingMode != ThinkingMode::Off;

        if (!hasApiKey && !hasCustomApiKeyEnv && !hasCustomModel && !hasThinking)
            return;

        emitter << YAML::Key << std::string(sectionName) << YAML::Value << YAML::BeginMap;
        if (hasApiKey)
            emitter << YAML::Key << "api_key" << YAML::Value << apiKey;
        if (hasCustomApiKeyEnv)
            emitter << YAML::Key << "api_key_env" << YAML::Value << apiKeyEnv;
        if (hasCustomModel)
            emitter << YAML::Key << "model" << YAML::Value << model;
        if (hasThinking)
            emitter << YAML::Key << "thinking_mode" << YAML::Value
                    << std::string(thinkingModeToString(thinkingMode));
        emitter << YAML::EndMap;
    }
} // namespace

auto loadAgentConfig(std::filesystem::path const& path) -> std::expected<AgentConfig, std::string>
{
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
}

auto loadAgentConfig() -> AgentConfig
{
    auto const home = std::getenv("HOME");
    if (!home)
        return AgentConfig {};

    auto const path = std::filesystem::path(home) / ".config" / "endo" / "agent.yml";
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
        std::filesystem::create_directories(path.parent_path());

        auto emitter = YAML::Emitter {};
        emitter << YAML::BeginMap;

        // Persist API keys, model preferences, and thinking mode per provider.
        auto const defaults = AgentConfig {};
        emitProviderSection(emitter,
                            "claude",
                            config.claude.apiKey,
                            config.claude.apiKeyEnv,
                            defaults.claude.apiKeyEnv,
                            config.claude.model,
                            defaults.claude.model,
                            config.claude.thinkingMode);
        emitProviderSection(emitter,
                            "openai",
                            config.openai.apiKey,
                            config.openai.apiKeyEnv,
                            defaults.openai.apiKeyEnv,
                            config.openai.model,
                            defaults.openai.model,
                            config.openai.thinkingMode);
        emitProviderSection(emitter,
                            "openai_compat",
                            config.openaiCompat.apiKey,
                            config.openaiCompat.apiKeyEnv,
                            defaults.openaiCompat.apiKeyEnv,
                            config.openaiCompat.model,
                            defaults.openaiCompat.model,
                            config.openaiCompat.thinkingMode);
        emitProviderSection(emitter,
                            "gemini",
                            config.gemini.apiKey,
                            config.gemini.apiKeyEnv,
                            defaults.gemini.apiKeyEnv,
                            config.gemini.model,
                            defaults.gemini.model,
                            config.gemini.thinkingMode);

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
    auto const* home = std::getenv("HOME");
    if (!home)
        return std::string("HOME environment variable not set");

    auto const path = std::filesystem::path(home) / ".config" / "endo" / "agent.yml";
    return saveAgentConfig(config, path);
}

} // namespace endo::agent
