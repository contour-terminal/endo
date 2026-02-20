// SPDX-License-Identifier: Apache-2.0
#include "ProviderFactory.hpp"

#include <http/HttpClient.hpp>

#include <agent/providers/ClaudeProvider.hpp>
#include <agent/providers/GeminiProvider.hpp>
#include <agent/providers/OpenAiProvider.hpp>

namespace endo::agent
{

ProviderFactory::ProviderFactory(http::HttpClient const& httpClient, AgentConfig const& config):
    _config(config)
{
    // Try to create Claude provider (stored key takes priority over env var)
    if (auto key = resolveProviderApiKey(config.claude.apiKey, config.claude.apiKeyEnv))
    {
        auto providerConfig = ClaudeProviderConfig {
            .apiKey = std::move(*key),
            .model = config.claude.model,
            .maxTokens = config.claude.maxTokens,
        };
        _providers.emplace("claude", std::make_unique<ClaudeProvider>(httpClient, std::move(providerConfig)));
    }

    // Try to create OpenAI provider (stored key takes priority over env var)
    if (auto key = resolveProviderApiKey(config.openai.apiKey, config.openai.apiKeyEnv))
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
            .apiKey =
                resolveProviderApiKey(config.openaiCompat.apiKey, config.openaiCompat.apiKeyEnv).value_or(""),
            .model = config.openaiCompat.model,
            .baseUrl = config.openaiCompat.baseUrl,
            .maxTokens = config.openaiCompat.maxTokens,
        };
        _providers.emplace("openai_compat",
                           std::make_unique<OpenAiProvider>(httpClient, std::move(providerConfig)));
    }

    // Try to create Gemini provider (stored key takes priority over env var)
    if (auto key = resolveProviderApiKey(config.gemini.apiKey, config.gemini.apiKeyEnv))
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

auto ProviderFactory::createProvider() const -> std::optional<OwnedProvider>
{
    if (_activeProviderName.empty())
        return std::nullopt;

    auto httpClient = std::make_unique<http::HttpClient>();
    auto& httpRef = *httpClient; // Stable reference — unique_ptr move doesn't invalidate the pointee.

    if (_activeProviderName == "claude")
    {
        if (auto key = resolveProviderApiKey(_config.claude.apiKey, _config.claude.apiKeyEnv))
        {
            auto providerConfig = ClaudeProviderConfig {
                .apiKey = std::move(*key),
                .model = _config.claude.model,
                .maxTokens = _config.claude.maxTokens,
            };
            auto provider = std::make_unique<ClaudeProvider>(httpRef, std::move(providerConfig));
            return OwnedProvider { .httpClient = std::move(httpClient), .provider = std::move(provider) };
        }
    }
    else if (_activeProviderName == "openai")
    {
        if (auto key = resolveProviderApiKey(_config.openai.apiKey, _config.openai.apiKeyEnv))
        {
            auto providerConfig = OpenAiProviderConfig {
                .apiKey = std::move(*key),
                .model = _config.openai.model,
                .baseUrl =
                    _config.openai.baseUrl.empty() ? "https://api.openai.com/v1" : _config.openai.baseUrl,
                .maxTokens = _config.openai.maxTokens,
            };
            auto provider = std::make_unique<OpenAiProvider>(httpRef, std::move(providerConfig));
            return OwnedProvider { .httpClient = std::move(httpClient), .provider = std::move(provider) };
        }
    }
    else if (_activeProviderName == "openai_compat")
    {
        if (!_config.openaiCompat.baseUrl.empty())
        {
            auto providerConfig = OpenAiProviderConfig {
                .apiKey = resolveProviderApiKey(_config.openaiCompat.apiKey, _config.openaiCompat.apiKeyEnv)
                              .value_or(""),
                .model = _config.openaiCompat.model,
                .baseUrl = _config.openaiCompat.baseUrl,
                .maxTokens = _config.openaiCompat.maxTokens,
            };
            auto provider = std::make_unique<OpenAiProvider>(httpRef, std::move(providerConfig));
            return OwnedProvider { .httpClient = std::move(httpClient), .provider = std::move(provider) };
        }
    }
    else if (_activeProviderName == "gemini")
    {
        if (auto key = resolveProviderApiKey(_config.gemini.apiKey, _config.gemini.apiKeyEnv))
        {
            auto providerConfig = GeminiProviderConfig {
                .apiKey = std::move(*key),
                .model = _config.gemini.model,
                .maxTokens = _config.gemini.maxTokens,
            };
            auto provider = std::make_unique<GeminiProvider>(httpRef, std::move(providerConfig));
            return OwnedProvider { .httpClient = std::move(httpClient), .provider = std::move(provider) };
        }
    }

    return std::nullopt;
}

} // namespace endo::agent
