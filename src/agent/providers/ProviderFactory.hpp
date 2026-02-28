// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <agent/AgentConfig.hpp>
#include <agent/providers/LlmProvider.hpp>

#if defined(ENDO_HAS_LOCAL_LLM) && ENDO_HAS_LOCAL_LLM
    #include <agent/providers/local/ModelManager.hpp>
#endif

namespace endo::http
{
class HttpClient;
}

namespace endo::agent
{

/// Owned provider with its own HttpClient, suitable for use on a worker thread.
///
/// Each worker thread needs its own CURL handle (HttpClient) since CURL easy handles
/// are not thread-safe. This struct bundles both together with proper ownership.
struct OwnedProvider
{
    std::unique_ptr<http::HttpClient> httpClient; ///< Owned HTTP client with its own CURL handle.
    std::unique_ptr<LlmProvider> provider;        ///< The LLM provider instance.
};

/// Manages all authenticated LLM providers and enables runtime switching.
///
/// On construction, the factory creates provider instances for every configured
/// provider that has a valid API key (or doesn't require one). The active provider
/// can be changed at runtime via switchProvider().
class ProviderFactory
{
  public:
    /// Constructs the factory, creating providers for all configured and authenticated entries.
    /// @param httpClient Shared HTTP client used by all providers.
    /// @param config     Agent configuration with provider settings.
    explicit ProviderFactory(http::HttpClient const& httpClient, AgentConfig const& config);

    /// Returns the currently active provider, or nullptr if no provider is authenticated.
    [[nodiscard]] auto activeProvider() -> LlmProvider*;

    /// Switches the active provider by name.
    /// @param name Provider name ("claude", "openai", "openai_compat", "gemini").
    /// @return true if the provider was found and is authenticated, false otherwise.
    [[nodiscard]] auto switchProvider(std::string_view name) -> bool;

    /// Returns the names of all providers that have valid API keys configured.
    [[nodiscard]] auto authenticatedProviders() const -> std::vector<std::string>;

    /// Returns the name of the currently active provider.
    [[nodiscard]] auto activeProviderName() const -> std::string const&;

    /// Creates a fresh provider instance with its own HttpClient for use on a worker thread.
    ///
    /// Returns the active provider type with a separate CURL handle, or nullopt if
    /// no provider is authenticated.
    /// @return An OwnedProvider with independent HttpClient and LlmProvider.
    [[nodiscard]] auto createProvider() const -> std::optional<OwnedProvider>;

  private:
    std::unordered_map<std::string, std::unique_ptr<LlmProvider>> _providers;
    std::string _activeProviderName;
    AgentConfig _config; ///< Saved config for createProvider().

#if defined(ENDO_HAS_LOCAL_LLM) && ENDO_HAS_LOCAL_LLM
    /// Shared model manager for local inference (model weights are thread-safe for reads).
    std::unique_ptr<local::ModelManager> _modelManager;
#endif
};

} // namespace endo::agent
