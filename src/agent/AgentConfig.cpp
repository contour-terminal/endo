// SPDX-License-Identifier: Apache-2.0
#include "AgentConfig.hpp"

#include <yaml-cpp/yaml.h>

#include <cstdlib>

namespace endo::agent
{

namespace
{
    void parseClaudeConfig(YAML::Node const& node, ClaudeConfig& config)
    {
        if (!node || !node.IsMap())
            return;
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
        if (node["api_key_env"])
            config.apiKeyEnv = node["api_key_env"].as<std::string>();
        if (node["model"])
            config.model = node["model"].as<std::string>();
        if (node["max_tokens"])
            config.maxTokens = node["max_tokens"].as<size_t>();
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

} // namespace endo::agent
