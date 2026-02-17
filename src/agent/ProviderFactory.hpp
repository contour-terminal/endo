// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <agent/AgentConfig.hpp>
#include <agent/LlmProvider.hpp>

namespace endo::http
{
class HttpClient;
}

namespace endo::agent
{

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

  private:
    std::unordered_map<std::string, std::unique_ptr<LlmProvider>> _providers;
    std::string _activeProviderName;
};

} // namespace endo::agent
