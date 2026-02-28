// SPDX-License-Identifier: Apache-2.0
#include "ProviderFactory.hpp"

#include <http/HttpClient.hpp>

#include <agent/auth/CopilotDeviceFlow.hpp>
#include <agent/auth/OAuthFlow.hpp>
#include <agent/providers/ClaudeProvider.hpp>
#include <agent/providers/CopilotProvider.hpp>
#include <agent/providers/GeminiProvider.hpp>
#include <agent/providers/OpenAiProvider.hpp>

#if defined(ENDO_HAS_LOCAL_LLM) && ENDO_HAS_LOCAL_LLM
    #include <agent/local/ChatTemplate.hpp>
    #include <agent/local/ModelManager.hpp>
    #include <agent/providers/LlamaCppProvider.hpp>
#endif

namespace endo::agent
{

namespace
{
    /// Attempts to resolve an OAuth access token, refreshing if expired.
    /// @return The access token, or nullopt if unavailable/refresh failed.
    auto resolveOAuthToken(OAuthStore const& oauthStore) -> std::optional<std::string>
    {
        if (!oauthStore.claude.has_value() || oauthStore.claude->accessToken.empty())
            return std::nullopt;

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
        return std::nullopt;
    }

    /// Resolves the Claude API token respecting the auth preference.
    /// @param config Agent configuration (contains authPreference: "auto", "oauth", "api_key").
    /// @param oauthStore Loaded OAuth credential store.
    /// @return The token string (OAuth access token or API key), or nullopt.
    auto resolveClaudeToken(AgentConfig const& config, OAuthStore const& oauthStore)
        -> std::optional<std::string>
    {
        auto const& pref = config.claude.authPreference;

        if (pref == "api_key")
        {
            // User explicitly wants API key — skip OAuth entirely.
            return resolveProviderApiKey(config.claude.apiKey, config.claude.apiKeyEnv);
        }

        if (pref == "oauth")
        {
            // User explicitly wants OAuth — skip API key entirely.
            return resolveOAuthToken(oauthStore);
        }

        // "auto" (default): OAuth > API key > env var.
        if (auto token = resolveOAuthToken(oauthStore))
            return token;

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

    /// Attempts to resolve a Gemini OAuth access token, refreshing if expired.
    /// @return The access token, or nullopt if unavailable/refresh failed.
    auto resolveGeminiOAuthToken(OAuthStore const& oauthStore) -> std::optional<std::string>
    {
        if (!oauthStore.gemini.has_value() || oauthStore.gemini->accessToken.empty())
            return std::nullopt;

        if (!isTokenExpired(*oauthStore.gemini))
            return oauthStore.gemini->accessToken;

        // Token expired — attempt refresh.
        auto refreshClient = http::HttpClient {};
        auto refreshed = refreshGoogleOAuthToken(refreshClient, oauthStore.gemini->refreshToken);
        if (refreshed.has_value())
        {
            // Persist the refreshed token.
            auto store = oauthStore;
            store.gemini = std::move(*refreshed);
            (void) saveOAuthStore(store);
            return store.gemini->accessToken;
        }
        return std::nullopt;
    }

    /// Resolves the Gemini API token respecting the auth preference.
    /// @return A pair of (token, isOAuth), or nullopt if neither source is available.
    auto resolveGeminiToken(AgentConfig const& config, OAuthStore const& oauthStore)
        -> std::optional<std::pair<std::string, bool>>
    {
        auto const& pref = config.gemini.authPreference;

        if (pref == "api_key")
        {
            if (auto key = resolveProviderApiKey(config.gemini.apiKey, config.gemini.apiKeyEnv))
                return std::pair { std::move(*key), false };
            return std::nullopt;
        }

        if (pref == "oauth")
        {
            if (auto token = resolveGeminiOAuthToken(oauthStore))
                return std::pair { std::move(*token), true };
            return std::nullopt;
        }

        // "auto" (default): OAuth > API key > env var.
        if (auto token = resolveGeminiOAuthToken(oauthStore))
            return std::pair { std::move(*token), true };

        if (auto key = resolveProviderApiKey(config.gemini.apiKey, config.gemini.apiKeyEnv))
            return std::pair { std::move(*key), false };

        return std::nullopt;
    }

    /// Resolves the GitHub OAuth token for Copilot from the OAuth store.
    auto resolveCopilotToken(OAuthStore const& oauthStore) -> std::optional<std::string>
    {
        if (!oauthStore.copilot.has_value() || oauthStore.copilot->accessToken.empty())
            return std::nullopt;
        return oauthStore.copilot->accessToken;
    }

    /// Creates a TokenRefresher that reloads the GitHub token from the OAuthStore.
    /// (The GitHub token itself is long-lived; this handles re-reading from disk
    /// in case another process updated it.)
    auto makeCopilotTokenRefresher() -> TokenRefresher
    {
        return []() -> std::expected<std::string, std::string> {
            auto store = loadOAuthStore();
            if (!store.copilot.has_value())
                return std::unexpected(std::string("No Copilot OAuth credentials stored"));
            return store.copilot->accessToken;
        };
    }

    /// Creates a TokenRefresher callback that refreshes the Gemini Google OAuth token
    /// and persists the new credentials to disk.
    auto makeGeminiTokenRefresher() -> TokenRefresher
    {
        return []() -> std::expected<std::string, std::string> {
            auto store = loadOAuthStore();
            if (!store.gemini.has_value())
                return std::unexpected(std::string("No Gemini OAuth credentials stored"));

            auto httpClient = http::HttpClient {};
            auto refreshed = refreshGoogleOAuthToken(httpClient, store.gemini->refreshToken);
            if (!refreshed.has_value())
                return std::unexpected(refreshed.error());

            auto const newAccessToken = refreshed->accessToken;
            store.gemini = std::move(*refreshed);
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
            .thinkingMode = config.claude.thinkingMode,
            .promptCaching = config.claude.promptCaching,
        };

        // Attach a token refresher when using an OAuth token.
        if (isOAuthToken(providerConfig.apiKey))
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
            .thinkingMode = config.openai.thinkingMode,
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
            .thinkingMode = config.openaiCompat.thinkingMode,
        };
        _providers.emplace("openai_compat",
                           std::make_unique<OpenAiProvider>(httpClient, std::move(providerConfig)));
    }

    // Try to create Gemini provider (OAuth > stored key > env var)
    if (auto geminiToken = resolveGeminiToken(config, oauthStore))
    {
        auto providerConfig = GeminiProviderConfig {
            .apiKey = std::move(geminiToken->first),
            .model = config.gemini.model,
            .maxTokens = config.gemini.maxTokens,
            .thinkingMode = config.gemini.thinkingMode,
            .useOAuth = geminiToken->second,
        };
        if (providerConfig.useOAuth)
            providerConfig.tokenRefresher = makeGeminiTokenRefresher();
        _providers.emplace("gemini", std::make_unique<GeminiProvider>(httpClient, std::move(providerConfig)));
    }

    // Try to create Copilot provider (OAuth only — requires GitHub token in OAuthStore).
    if (auto ghToken = resolveCopilotToken(oauthStore))
    {
        auto providerConfig = CopilotProviderConfig {
            .githubToken = std::move(*ghToken),
            .model = config.copilot.model,
            .maxTokens = config.copilot.maxTokens,
            .thinkingMode = config.copilot.thinkingMode,
            .tokenRefresher = makeCopilotTokenRefresher(),
        };
        _providers.emplace("copilot",
                           std::make_unique<CopilotProvider>(httpClient, std::move(providerConfig)));
    }

#if defined(ENDO_HAS_LOCAL_LLM) && ENDO_HAS_LOCAL_LLM
    // Try to create local llama.cpp provider (requires a model path).
    if (!config.local.modelPath.empty())
    {
        _modelManager = std::make_unique<local::ModelManager>(
            config.local.modelDir, config.local.gpuLayers, config.local.flashAttention);

        if (auto error = _modelManager->loadModel(config.local.modelPath); !error)
        {
            auto providerConfig = LlamaCppProviderConfig {
                .contextSize = config.local.contextSize,
                .batchSize = config.local.batchSize,
                .temperature = config.local.temperature,
                .topP = config.local.topP,
                .topK = config.local.topK,
                .repeatPenalty = config.local.repeatPenalty,
                .maxTokens = config.local.maxTokens,
                .flashAttention = config.local.flashAttention,
            };
            if (!config.local.chatTemplate.empty())
            {
                providerConfig.chatTemplateOverride =
                    local::chatTemplateFromString(config.local.chatTemplate);
                providerConfig.useChatTemplateOverride = true;
            }
            _providers.emplace(
                "local", std::make_unique<LlamaCppProvider>(*_modelManager, std::move(providerConfig)));
        }
    }
#endif

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
                .thinkingMode = _config.claude.thinkingMode,
                .promptCaching = _config.claude.promptCaching,
            };
            if (isOAuthToken(providerConfig.apiKey))
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
                .thinkingMode = _config.openai.thinkingMode,
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
                .thinkingMode = _config.openaiCompat.thinkingMode,
            };
            auto provider = std::make_unique<OpenAiProvider>(httpRef, std::move(providerConfig));
            return OwnedProvider { .httpClient = std::move(httpClient), .provider = std::move(provider) };
        }
    }
    else if (_activeProviderName == "gemini")
    {
        auto const oauthStore = loadOAuthStore();
        if (auto geminiToken = resolveGeminiToken(_config, oauthStore))
        {
            auto providerConfig = GeminiProviderConfig {
                .apiKey = std::move(geminiToken->first),
                .model = _config.gemini.model,
                .maxTokens = _config.gemini.maxTokens,
                .thinkingMode = _config.gemini.thinkingMode,
                .useOAuth = geminiToken->second,
            };
            if (providerConfig.useOAuth)
                providerConfig.tokenRefresher = makeGeminiTokenRefresher();
            auto provider = std::make_unique<GeminiProvider>(httpRef, std::move(providerConfig));
            return OwnedProvider { .httpClient = std::move(httpClient), .provider = std::move(provider) };
        }
    }
    else if (_activeProviderName == "copilot")
    {
        auto const oauthStore = loadOAuthStore();
        if (auto ghToken = resolveCopilotToken(oauthStore))
        {
            auto providerConfig = CopilotProviderConfig {
                .githubToken = std::move(*ghToken),
                .model = _config.copilot.model,
                .maxTokens = _config.copilot.maxTokens,
                .thinkingMode = _config.copilot.thinkingMode,
                .tokenRefresher = makeCopilotTokenRefresher(),
            };
            auto provider = std::make_unique<CopilotProvider>(httpRef, std::move(providerConfig));
            return OwnedProvider { .httpClient = std::move(httpClient), .provider = std::move(provider) };
        }
    }
#if defined(ENDO_HAS_LOCAL_LLM) && ENDO_HAS_LOCAL_LLM
    else if (_activeProviderName == "local")
    {
        if (_modelManager && _modelManager->isLoaded())
        {
            auto providerConfig = LlamaCppProviderConfig {
                .contextSize = _config.local.contextSize,
                .batchSize = _config.local.batchSize,
                .temperature = _config.local.temperature,
                .topP = _config.local.topP,
                .topK = _config.local.topK,
                .repeatPenalty = _config.local.repeatPenalty,
                .maxTokens = _config.local.maxTokens,
                .flashAttention = _config.local.flashAttention,
            };
            if (!_config.local.chatTemplate.empty())
            {
                providerConfig.chatTemplateOverride =
                    local::chatTemplateFromString(_config.local.chatTemplate);
                providerConfig.useChatTemplateOverride = true;
            }
            // Local provider does not need an HttpClient — set it to nullptr.
            auto provider =
                std::make_unique<LlamaCppProvider>(*_modelManager, std::move(providerConfig));
            return OwnedProvider { .httpClient = nullptr, .provider = std::move(provider) };
        }
    }
#endif

    return std::nullopt;
}

} // namespace endo::agent
