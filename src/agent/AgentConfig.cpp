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
    }

    void emitClaudeConfig(YAML::Emitter& emitter, ClaudeConfig const& config)
    {
        auto const defaults = ClaudeConfig {};
        auto hasNonDefault = !config.apiKey.empty() || config.apiKeyEnv != defaults.apiKeyEnv
                             || config.model != defaults.model || config.maxTokens != defaults.maxTokens;
        if (!hasNonDefault)
            return;
        emitter << YAML::Key << "claude" << YAML::Value << YAML::BeginMap;
        if (!config.apiKey.empty())
            emitter << YAML::Key << "api_key" << YAML::Value << config.apiKey;
        if (config.apiKeyEnv != defaults.apiKeyEnv)
            emitter << YAML::Key << "api_key_env" << YAML::Value << config.apiKeyEnv;
        if (config.model != defaults.model)
            emitter << YAML::Key << "model" << YAML::Value << config.model;
        if (config.maxTokens != defaults.maxTokens)
            emitter << YAML::Key << "max_tokens" << YAML::Value << config.maxTokens;
        emitter << YAML::EndMap;
    }

    void emitOpenAiConfig(YAML::Emitter& emitter,
                          std::string_view sectionName,
                          OpenAiConfig const& config,
                          OpenAiConfig const& defaults)
    {
        auto hasNonDefault = !config.apiKey.empty() || config.apiKeyEnv != defaults.apiKeyEnv
                             || config.model != defaults.model || !config.baseUrl.empty()
                             || config.maxTokens != defaults.maxTokens;
        if (!hasNonDefault)
            return;
        emitter << YAML::Key << std::string(sectionName) << YAML::Value << YAML::BeginMap;
        if (!config.apiKey.empty())
            emitter << YAML::Key << "api_key" << YAML::Value << config.apiKey;
        if (config.apiKeyEnv != defaults.apiKeyEnv)
            emitter << YAML::Key << "api_key_env" << YAML::Value << config.apiKeyEnv;
        if (config.model != defaults.model)
            emitter << YAML::Key << "model" << YAML::Value << config.model;
        if (!config.baseUrl.empty())
            emitter << YAML::Key << "base_url" << YAML::Value << config.baseUrl;
        if (config.maxTokens != defaults.maxTokens)
            emitter << YAML::Key << "max_tokens" << YAML::Value << config.maxTokens;
        emitter << YAML::EndMap;
    }

    void emitGeminiConfig(YAML::Emitter& emitter, GeminiConfig const& config)
    {
        auto const defaults = GeminiConfig {};
        auto hasNonDefault = !config.apiKey.empty() || config.apiKeyEnv != defaults.apiKeyEnv
                             || config.model != defaults.model || config.maxTokens != defaults.maxTokens;
        if (!hasNonDefault)
            return;
        emitter << YAML::Key << "gemini" << YAML::Value << YAML::BeginMap;
        if (!config.apiKey.empty())
            emitter << YAML::Key << "api_key" << YAML::Value << config.apiKey;
        if (config.apiKeyEnv != defaults.apiKeyEnv)
            emitter << YAML::Key << "api_key_env" << YAML::Value << config.apiKeyEnv;
        if (config.model != defaults.model)
            emitter << YAML::Key << "model" << YAML::Value << config.model;
        if (config.maxTokens != defaults.maxTokens)
            emitter << YAML::Key << "max_tokens" << YAML::Value << config.maxTokens;
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

        parseClaudeConfig(root["claude"], config.claude);
        parseOpenAiConfig(root["openai"], config.openai);
        parseOpenAiConfig(root["openai_compat"], config.openaiCompat);
        parseGeminiConfig(root["gemini"], config.gemini);

        if (root["max_tool_result_size"])
            config.maxToolResultSize = root["max_tool_result_size"].as<size_t>();

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

        auto const defaults = AgentConfig {};
        if (config.activeProvider != defaults.activeProvider)
            emitter << YAML::Key << "active_provider" << YAML::Value << config.activeProvider;

        emitClaudeConfig(emitter, config.claude);
        emitOpenAiConfig(emitter, "openai", config.openai, defaults.openai);
        emitOpenAiConfig(emitter, "openai_compat", config.openaiCompat, defaults.openaiCompat);
        emitGeminiConfig(emitter, config.gemini);

        if (config.maxToolResultSize != defaults.maxToolResultSize)
            emitter << YAML::Key << "max_tool_result_size" << YAML::Value << config.maxToolResultSize;

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
