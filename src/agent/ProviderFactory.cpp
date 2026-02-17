// SPDX-License-Identifier: Apache-2.0
#include "ProviderFactory.hpp"

#include <http/HttpClient.hpp>

#include <agent/ClaudeProvider.hpp>
#include <agent/GeminiProvider.hpp>
#include <agent/OpenAiProvider.hpp>

namespace endo::agent
{

ProviderFactory::ProviderFactory(http::HttpClient const& httpClient, AgentConfig const& config)
{
    // Try to create Claude provider
    if (auto key = resolveApiKey(config.claude.apiKeyEnv))
    {
        auto providerConfig = ClaudeProviderConfig {
            .apiKey = std::move(*key),
            .model = config.claude.model,
            .maxTokens = config.claude.maxTokens,
        };
        _providers.emplace("claude", std::make_unique<ClaudeProvider>(httpClient, std::move(providerConfig)));
    }

    // Try to create OpenAI provider
    if (auto key = resolveApiKey(config.openai.apiKeyEnv))
    {
        auto providerConfig = OpenAiProviderConfig {
            .apiKey = std::move(*key),
            .model = config.openai.model,
            .baseUrl = config.openai.baseUrl.empty() ? "https://api.openai.com/v1" : config.openai.baseUrl,
            .maxTokens = config.openai.maxTokens,
        };
        _providers.emplace("openai", std::make_unique<OpenAiProvider>(httpClient, std::move(providerConfig)));
    }

    // Try to create OpenAI-compatible provider (API key optional for local models)
    if (!config.openaiCompat.baseUrl.empty())
    {
        auto providerConfig = OpenAiProviderConfig {
            .apiKey = resolveApiKey(config.openaiCompat.apiKeyEnv).value_or(""),
            .model = config.openaiCompat.model,
            .baseUrl = config.openaiCompat.baseUrl,
            .maxTokens = config.openaiCompat.maxTokens,
        };
        _providers.emplace("openai_compat",
                           std::make_unique<OpenAiProvider>(httpClient, std::move(providerConfig)));
    }

    // Try to create Gemini provider
    if (auto key = resolveApiKey(config.gemini.apiKeyEnv))
    {
        auto providerConfig = GeminiProviderConfig {
            .apiKey = std::move(*key),
            .model = config.gemini.model,
            .maxTokens = config.gemini.maxTokens,
        };
        _providers.emplace("gemini", std::make_unique<GeminiProvider>(httpClient, std::move(providerConfig)));
    }

    // Set active provider: prefer config.activeProvider if authenticated, otherwise first available
    if (_providers.contains(config.activeProvider))
    {
        _activeProviderName = config.activeProvider;
    }
    else if (!_providers.empty())
    {
        _activeProviderName = _providers.begin()->first;
    }
}

auto ProviderFactory::activeProvider() -> LlmProvider*
{
    auto const it = _providers.find(_activeProviderName);
    return (it != _providers.end()) ? it->second.get() : nullptr;
}

auto ProviderFactory::switchProvider(std::string_view name) -> bool
{
    auto const nameStr = std::string(name);
    if (!_providers.contains(nameStr))
        return false;
    _activeProviderName = nameStr;
    return true;
}

auto ProviderFactory::authenticatedProviders() const -> std::vector<std::string>
{
    auto result = std::vector<std::string> {};
    result.reserve(_providers.size());
    for (auto const& [name, _]: _providers)
        result.push_back(name);
    return result;
}

auto ProviderFactory::activeProviderName() const -> std::string const&
{
    return _activeProviderName;
}

} // namespace endo::agent
