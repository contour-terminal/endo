// SPDX-License-Identifier: Apache-2.0
#include "ProviderFactory.hpp"

#include <http/HttpClient.hpp>

#include <agent/OAuthFlow.hpp>
#include <agent/providers/ClaudeProvider.hpp>
#include <agent/providers/GeminiProvider.hpp>
#include <agent/providers/OpenAiProvider.hpp>

namespace endo::agent
{

namespace
{
    /// Resolves the Claude API token: OAuth credentials take priority, then API key.
    /// @return The token string (OAuth access token or API key), or nullopt.
    auto resolveClaudeToken(AgentConfig const& config, OAuthStore const& oauthStore)
        -> std::optional<std::string>
    {
        // Priority 1: OAuth credentials from agent-oauth.yaml.
        if (oauthStore.claude.has_value() && !oauthStore.claude->accessToken.empty())
        {
            if (!isTokenExpired(*oauthStore.claude))
                return oauthStore.claude->accessToken;

            // Token expired — attempt refresh.
            auto refreshClient = http::HttpClient {};
            auto refreshed = refreshOAuthToken(refreshClient, oauthStore.claude->refreshToken);
            if (refreshed.has_value())
            {
                // Persist the refreshed token.
                auto store = oauthStore;
                store.claude = std::move(*refreshed);
                (void) saveOAuthStore(store);
                return store.claude->accessToken;
            }
            // Refresh failed — fall through to API key.
        }

        // Priority 2: API key (stored or env var).
        return resolveProviderApiKey(config.claude.apiKey, config.claude.apiKeyEnv);
    }

    /// Creates a TokenRefresher callback that refreshes the Claude OAuth token
    /// and persists the new credentials to disk.
    auto makeClaudeTokenRefresher() -> TokenRefresher
    {
        return []() -> std::expected<std::string, std::string> {
            auto store = loadOAuthStore();
            if (!store.claude.has_value())
                return std::unexpected(std::string("No OAuth credentials stored"));

            auto httpClient = http::HttpClient {};
            auto refreshed = refreshOAuthToken(httpClient, store.claude->refreshToken);
            if (!refreshed.has_value())
                return std::unexpected(refreshed.error());

            auto const newAccessToken = refreshed->accessToken;
            store.claude = std::move(*refreshed);
            (void) saveOAuthStore(store);
            return newAccessToken;
        };
    }
} // namespace

ProviderFactory::ProviderFactory(http::HttpClient const& httpClient, AgentConfig const& config):
    _config(config)
{
    // Load OAuth credentials for all providers.
    auto const oauthStore = loadOAuthStore();

    // Try to create Claude provider (OAuth > stored key > env var).
    if (auto token = resolveClaudeToken(config, oauthStore))
    {
        auto providerConfig = ClaudeProviderConfig {
            .apiKey = std::move(*token),
            .model = config.claude.model,
            .maxTokens = config.claude.maxTokens,
        };

        // If we're using an OAuth token, attach a refresher.
        if (oauthStore.claude.has_value())
            providerConfig.tokenRefresher = makeClaudeTokenRefresher();

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
        auto const oauthStore = loadOAuthStore();
        if (auto token = resolveClaudeToken(_config, oauthStore))
        {
            auto providerConfig = ClaudeProviderConfig {
                .apiKey = std::move(*token),
                .model = _config.claude.model,
                .maxTokens = _config.claude.maxTokens,
            };
            if (oauthStore.claude.has_value())
                providerConfig.tokenRefresher = makeClaudeTokenRefresher();
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
