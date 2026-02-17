// SPDX-License-Identifier: Apache-2.0
#include "LoginCommand.hpp"

#include <http/HttpClient.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <print>
#include <string>
#include <string_view>

#if !defined(_WIN32)
    #include <unistd.h>
#endif

#include <agent/AgentConfig.hpp>
#include <agent/TerminalInput.hpp>

using namespace std::string_view_literals;

namespace endo::agent
{

namespace
{

    /// Known provider names and their display labels.
    struct ProviderInfo
    {
        std::string_view name;        ///< Config key ("claude", "openai", "gemini", "openai_compat").
        std::string_view label;       ///< Display label.
        std::string_view apiKeyUrl;   ///< URL to create an API key.
        std::string_view validateUrl; ///< URL for key validation (GET request).
    };

    constexpr auto KnownProviders = std::array<ProviderInfo, 3> { {
        { "claude"sv,
          "Claude (Anthropic)"sv,
          "https://console.anthropic.com/settings/keys"sv,
          "https://api.anthropic.com/v1/models"sv },
        { "openai"sv,
          "OpenAI"sv,
          "https://platform.openai.com/api-keys"sv,
          "https://api.openai.com/v1/models"sv },
        { "gemini"sv,
          "Gemini (Google)"sv,
          "https://aistudio.google.com/apikey"sv,
          "https://generativelanguage.googleapis.com/v1beta/models"sv },
    } };

    /// Finds a ProviderInfo by name, or nullptr if not found.
    auto findProvider(std::string_view name) -> ProviderInfo const*
    {
        auto const it = std::ranges::find(KnownProviders, name, &ProviderInfo::name);
        return (it != KnownProviders.end()) ? &(*it) : nullptr;
    }

    /// Prompts the user to select a provider from a numbered menu.
    /// @return The selected provider name, or empty on error.
    auto promptProviderSelection() -> std::string
    {
        std::print("\nSelect a provider to authenticate:\n");
        for (size_t i = 0; i < KnownProviders.size(); ++i)
            std::print("  [{}] {}\n", i + 1, KnownProviders[i].label);
        std::print("\nEnter choice [1-{}]: ", KnownProviders.size());

        auto input = std::string {};
        if (!std::getline(std::cin, input) || input.empty())
            return {};

        auto const choice = std::strtol(input.c_str(), nullptr, 10);
        if (choice < 1 || static_cast<size_t>(choice) > KnownProviders.size())
        {
            std::print(stderr, "Invalid choice.\n");
            return {};
        }
        return std::string(KnownProviders[static_cast<size_t>(choice - 1)].name);
    }

    /// Builds the validation request headers for a provider.
    auto makeValidationHeaders(std::string_view provider, std::string_view apiKey) -> std::vector<std::string>
    {
        if (provider == "claude")
            return { std::string("x-api-key: ") + std::string(apiKey), "anthropic-version: 2023-06-01" };
        if (provider == "openai")
            return { std::string("Authorization: Bearer ") + std::string(apiKey) };
        return {};
    }

    /// Builds the validation URL for a provider.
    auto makeValidationUrl(ProviderInfo const& info, std::string_view apiKey) -> std::string
    {
        auto url = std::string(info.validateUrl);
        if (info.name == "gemini")
            url += std::string("?key=") + std::string(apiKey);
        return url;
    }

    /// Validates an API key by making a lightweight test request.
    /// @return true if the key is valid (HTTP 200).
    auto validateApiKey(std::string_view provider, std::string_view apiKey) -> bool
    {
        auto const* info = findProvider(provider);
        if (!info)
            return false;

        auto client = http::HttpClient {};
        auto const url = makeValidationUrl(*info, apiKey);
        auto const headers = makeValidationHeaders(provider, apiKey);
        auto const result = client.get(url, headers);

        if (!result.has_value())
        {
            std::print(stderr, "Network error: {}\n", result.error().message);
            return false;
        }

        return result->statusCode == 200;
    }

    /// Sets the API key in the appropriate provider config field.
    void setProviderApiKey(AgentConfig& config, std::string_view provider, std::string const& apiKey)
    {
        if (provider == "claude")
            config.claude.apiKey = apiKey;
        else if (provider == "openai")
            config.openai.apiKey = apiKey;
        else if (provider == "gemini")
            config.gemini.apiKey = apiKey;
    }

    /// Clears the stored API key for a provider.
    void clearProviderApiKey(AgentConfig& config, std::string_view provider)
    {
        setProviderApiKey(config, provider, {});
    }

    /// Checks if a provider has an API key available (stored or via env var).
    auto isProviderAuthenticated(AgentConfig const& config, std::string_view provider) -> bool
    {
        if (provider == "claude")
            return resolveProviderApiKey(config.claude.apiKey, config.claude.apiKeyEnv).has_value();
        if (provider == "openai")
            return resolveProviderApiKey(config.openai.apiKey, config.openai.apiKeyEnv).has_value();
        if (provider == "gemini")
            return resolveProviderApiKey(config.gemini.apiKey, config.gemini.apiKeyEnv).has_value();
        if (provider == "openai_compat")
            return resolveProviderApiKey(config.openaiCompat.apiKey, config.openaiCompat.apiKeyEnv)
                .has_value();
        return false;
    }

    /// Returns the source of the API key for a provider ("config file", "env var", or empty).
    auto getKeySource(AgentConfig const& config, std::string_view provider) -> std::string_view
    {
        auto checkSource = [](std::string const& storedKey, std::string const& envName) -> std::string_view {
            if (!storedKey.empty())
                return "config file"sv;
            if (resolveApiKey(envName).has_value())
                return "env var"sv;
            return {};
        };

        if (provider == "claude")
            return checkSource(config.claude.apiKey, config.claude.apiKeyEnv);
        if (provider == "openai")
            return checkSource(config.openai.apiKey, config.openai.apiKeyEnv);
        if (provider == "gemini")
            return checkSource(config.gemini.apiKey, config.gemini.apiKeyEnv);
        if (provider == "openai_compat")
            return checkSource(config.openaiCompat.apiKey, config.openaiCompat.apiKeyEnv);
        return {};
    }

} // namespace

auto runLoginCommand(std::string_view providerHint) -> int
{
    auto config = loadAgentConfig();

    auto const providerName = providerHint.empty() ? promptProviderSelection() : std::string(providerHint);
    if (providerName.empty())
        return EXIT_FAILURE;

    auto const* info = findProvider(providerName);
    if (!info)
    {
        std::print(stderr, "Unknown provider: {}\n", providerName);
        std::print(stderr, "Available providers: claude, openai, gemini\n");
        return EXIT_FAILURE;
    }

    // Open browser to the API key page
    std::print("\nOpening {} to create an API key...\n", info->apiKeyUrl);
    if (!openBrowser(info->apiKeyUrl))
        std::print("Could not open browser. Please visit: {}\n", info->apiKeyUrl);

    // Read the API key with hidden input
    auto const apiKey = readSecretLine("\nPaste your API key: ");
    if (!apiKey.has_value() || apiKey->empty())
    {
        std::print(stderr, "No API key provided.\n");
        return EXIT_FAILURE;
    }

    // Validate the key
    std::print("Validating API key...");
    if (!validateApiKey(providerName, *apiKey))
    {
        std::print(stderr, "\nAuthentication failed: invalid API key.\n");
        return EXIT_FAILURE;
    }
    std::print(" OK\n");

    // Save to config
    setProviderApiKey(config, providerName, *apiKey);
    config.activeProvider = providerName;

    if (auto error = saveAgentConfig(config))
    {
        std::print(stderr, "Failed to save configuration: {}\n", *error);
        return EXIT_FAILURE;
    }

    std::print("Authenticated as {}. Configuration saved to ~/.config/endo/agent.yml\n", providerName);
    return EXIT_SUCCESS;
}

auto runStatusCommand() -> int
{
    auto const config = loadAgentConfig();

    // Detect whether stdout supports color (TTY check).
#if defined(_WIN32)
    auto const useColor = _isatty(_fileno(stdout));
#else
    auto const useColor = isatty(STDOUT_FILENO) != 0;
#endif

    // ANSI SGR sequences — empty when color is disabled.
    auto const reset = useColor ? "\033[0m"sv : ""sv;
    auto const bold = useColor ? "\033[1m"sv : ""sv;
    auto const dim = useColor ? "\033[2m"sv : ""sv;
    auto const green = useColor ? "\033[32m"sv : ""sv;
    auto const red = useColor ? "\033[31m"sv : ""sv;
    auto const yellow = useColor ? "\033[33m"sv : ""sv;
    auto const cyan = useColor ? "\033[36m"sv : ""sv;
    auto const magenta = useColor ? "\033[35m"sv : ""sv;

    std::print("\n{}{:<16}{:<18}{:<14}{}{}\n", bold, "Provider", "Status", "Source", "Active", reset);
    std::print("{}{:─<16}{:─<18}{:─<14}{:─<8}{}\n", dim, "", "", "", "", reset);

    auto const allProviders = std::array { "claude"sv, "openai"sv, "gemini"sv, "openai_compat"sv };

    for (auto const& provider: allProviders)
    {
        auto const authenticated = isProviderAuthenticated(config, provider);
        auto const source = getKeySource(config, provider);
        auto const isActive = (config.activeProvider == provider);

        // Provider name: bold+cyan if active, normal otherwise.
        auto const nameColor = isActive ? cyan : ""sv;
        auto const nameBold = isActive ? bold : ""sv;

        // Status: green for authenticated, red for not configured.
        auto const statusColor = authenticated ? green : red;
        auto const statusText = authenticated ? "authenticated"sv : "not configured"sv;

        // Source: dim yellow.
        auto const sourceColor = authenticated ? yellow : ""sv;
        auto const sourceText = authenticated ? source : ""sv;

        // Active marker: bold magenta.
        auto const activeText = isActive ? "<- active"sv : ""sv;
        auto const activeColor = isActive ? magenta : ""sv;
        auto const activeBold = isActive ? bold : ""sv;

        std::print("{}{}{:<16}{}{}{:<18}{}{}{:<14}{}{}{}{}{}\n",
                   nameBold,
                   nameColor,
                   provider,
                   reset,
                   statusColor,
                   statusText,
                   reset,
                   sourceColor,
                   sourceText,
                   reset,
                   activeBold,
                   activeColor,
                   activeText,
                   reset);
    }
    std::print("\n");
    return EXIT_SUCCESS;
}

auto runSwitchCommand(std::string_view providerHint) -> int
{
    auto config = loadAgentConfig();

    if (!providerHint.empty())
    {
        // Direct switch to named provider
        if (!isProviderAuthenticated(config, providerHint))
        {
            std::print(stderr,
                       "Provider '{}' is not authenticated. Run: endo agent login {}\n",
                       providerHint,
                       providerHint);
            return EXIT_FAILURE;
        }

        config.activeProvider = std::string(providerHint);
        if (auto error = saveAgentConfig(config))
        {
            std::print(stderr, "Failed to save configuration: {}\n", *error);
            return EXIT_FAILURE;
        }
        std::print("Switched active provider to {}.\n", providerHint);
        return EXIT_SUCCESS;
    }

    // Interactive: show menu of authenticated providers
    auto authenticatedProviders = std::vector<std::string> {};
    auto const allProviders = std::array { "claude"sv, "openai"sv, "gemini"sv, "openai_compat"sv };

    for (auto const& p: allProviders)
    {
        if (isProviderAuthenticated(config, p))
            authenticatedProviders.emplace_back(p);
    }

    if (authenticatedProviders.empty())
    {
        std::print(stderr, "No providers are authenticated. Run: endo agent login\n");
        return EXIT_FAILURE;
    }

    std::print("\nAuthenticated providers:\n");
    for (size_t i = 0; i < authenticatedProviders.size(); ++i)
    {
        auto const& name = authenticatedProviders[i];
        auto const isCurrent = (name == config.activeProvider);
        std::print("  [{}] {}{}\n", i + 1, name, isCurrent ? " (current)" : "");
    }

    std::print("\nSelect provider [1-{}]: ", authenticatedProviders.size());

    auto input = std::string {};
    if (!std::getline(std::cin, input) || input.empty())
        return EXIT_FAILURE;

    auto const choice = std::strtol(input.c_str(), nullptr, 10);
    if (choice < 1 || static_cast<size_t>(choice) > authenticatedProviders.size())
    {
        std::print(stderr, "Invalid choice.\n");
        return EXIT_FAILURE;
    }

    auto const& selected = authenticatedProviders[static_cast<size_t>(choice - 1)];
    config.activeProvider = selected;

    if (auto error = saveAgentConfig(config))
    {
        std::print(stderr, "Failed to save configuration: {}\n", *error);
        return EXIT_FAILURE;
    }

    std::print("Switched active provider to {}.\n", selected);
    return EXIT_SUCCESS;
}

auto runLogoutCommand(std::string_view providerHint) -> int
{
    auto config = loadAgentConfig();

    auto const providerName = providerHint.empty() ? promptProviderSelection() : std::string(providerHint);
    if (providerName.empty())
        return EXIT_FAILURE;

    auto const* info = findProvider(providerName);
    if (!info)
    {
        std::print(stderr, "Unknown provider: {}\n", providerName);
        return EXIT_FAILURE;
    }

    clearProviderApiKey(config, providerName);

    if (auto error = saveAgentConfig(config))
    {
        std::print(stderr, "Failed to save configuration: {}\n", *error);
        return EXIT_FAILURE;
    }

    std::print("Removed stored API key for {}.\n", providerName);
    return EXIT_SUCCESS;
}

} // namespace endo::agent
