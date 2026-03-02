// SPDX-License-Identifier: Apache-2.0
#include "LoginCommand.hpp"

#include <http/HttpClient.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <format>
#include <print>
#include <string>
#include <string_view>

#if defined(_WIN32)
    #include <io.h>
#else
    #include <unistd.h>
#endif

#include <agent/AgentConfig.hpp>
#include <agent/auth/CopilotDeviceFlow.hpp>
#include <agent/auth/OAuthCallbackServer.hpp>
#include <agent/auth/OAuthFlow.hpp>
#include <agent/auth/TerminalInput.hpp>

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
        bool supportsOAuth = false;   ///< Whether this provider supports OAuth login.
    };

    constexpr auto KnownProviders = std::array<ProviderInfo, 4> { {
        { .name = "claude"sv,
          .label = "Claude (Anthropic)"sv,
          .apiKeyUrl = "https://console.anthropic.com/settings/keys"sv,
          .validateUrl = "https://api.anthropic.com/v1/models"sv,
          .supportsOAuth = true },
        { .name = "openai"sv,
          .label = "OpenAI"sv,
          .apiKeyUrl = "https://platform.openai.com/api-keys"sv,
          .validateUrl = "https://api.openai.com/v1/models"sv,
          .supportsOAuth = false },
        { .name = "gemini"sv,
          .label = "Gemini (Google)"sv,
          .apiKeyUrl = "https://aistudio.google.com/apikey"sv,
          .validateUrl = "https://generativelanguage.googleapis.com/v1beta/models"sv,
          .supportsOAuth = true },
        { .name = "copilot"sv,
          .label = "GitHub Copilot"sv,
          .apiKeyUrl = ""sv,
          .validateUrl = ""sv,
          .supportsOAuth = true },
    } };

    /// Finds a ProviderInfo by name, or nullptr if not found.
    auto findProvider(std::string_view name) -> ProviderInfo const*
    {
        const auto* const it = std::ranges::find(KnownProviders, name, &ProviderInfo::name);
        return (it != KnownProviders.end()) ? &(*it) : nullptr;
    }

    /// Prompts the user to select a provider using TUI single-select.
    /// @return The selected provider name, or empty on cancel.
    auto promptProviderSelection() -> std::string
    {
        constexpr auto labels = std::array {
            "Claude (Anthropic)"sv,
            "OpenAI"sv,
            "Gemini (Google)"sv,
            "GitHub Copilot"sv,
        };
        auto const sel = askSingleSelect("Select a provider to authenticate:", labels);
        if (!sel)
            return {};
        return std::string(KnownProviders[*sel].name);
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
        // Copilot does not use API keys — OAuth only.
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
        // Copilot uses OAuth only — API key check is not applicable.
        return false;
    }

    /// Returns the source of authentication for a provider.
    auto getAuthSource(AgentConfig const& config, std::string_view provider, OAuthStore const& oauthStore)
        -> std::string
    {
        // Local provider: show model path as the "source".
        if (provider == "local")
            return config.local.modelPath.empty() ? std::string {} : "model: " + config.local.modelPath;

        // Check OAuth first.
        auto const* oauthCreds = [&]() -> OAuthCredentials const* {
            if (provider == "claude" && oauthStore.claude.has_value())
                return &*oauthStore.claude;
            if (provider == "openai" && oauthStore.openai.has_value())
                return &*oauthStore.openai;
            if (provider == "gemini" && oauthStore.gemini.has_value())
                return &*oauthStore.gemini;
            if (provider == "copilot" && oauthStore.copilot.has_value())
                return &*oauthStore.copilot;
            return nullptr;
        }();

        if (oauthCreds && !oauthCreds->accessToken.empty())
        {
            auto source = std::string("OAuth");
            if (!oauthCreds->authMode.empty())
                source += " (" + oauthCreds->authMode + ")";
            if (isTokenExpired(*oauthCreds))
                source += " [expired]";
            return source;
        }

        // Check API key sources.
        auto checkApiKeySource = [](std::string const& storedKey, std::string const& envName) -> std::string {
            if (!storedKey.empty())
                return "config file";
            if (resolveApiKey(envName).has_value())
                return "env var";
            return {};
        };

        if (provider == "claude")
            return checkApiKeySource(config.claude.apiKey, config.claude.apiKeyEnv);
        if (provider == "openai")
            return checkApiKeySource(config.openai.apiKey, config.openai.apiKeyEnv);
        if (provider == "gemini")
            return checkApiKeySource(config.gemini.apiKey, config.gemini.apiKeyEnv);
        if (provider == "openai_compat")
            return checkApiKeySource(config.openaiCompat.apiKey, config.openaiCompat.apiKeyEnv);
        return {};
    }

    /// Checks whether a provider is authenticated via any method (OAuth or API key).
    auto isProviderAuthenticatedFull(AgentConfig const& config,
                                     std::string_view provider,
                                     OAuthStore const& oauthStore) -> bool
    {
        // Local provider is "authenticated" if a model path is configured.
        if (provider == "local")
            return !config.local.modelPath.empty();

        // Check OAuth.
        if (provider == "claude" && oauthStore.claude.has_value() && !oauthStore.claude->accessToken.empty())
            return true;
        if (provider == "openai" && oauthStore.openai.has_value() && !oauthStore.openai->accessToken.empty())
            return true;
        if (provider == "gemini" && oauthStore.gemini.has_value() && !oauthStore.gemini->accessToken.empty())
            return true;
        if (provider == "copilot" && oauthStore.copilot.has_value()
            && !oauthStore.copilot->accessToken.empty())
            return true;

        // Check API key.
        return isProviderAuthenticated(config, provider);
    }

    // ── API Key Login Flow ───────────────────────────────────────────────────

    auto runApiKeyLoginFlow(std::string_view providerName, ProviderInfo const& info) -> int
    {
        auto config = loadAgentConfig();

        // Open browser to the API key page.
        std::print("\nOpening {} to create an API key...\n", info.apiKeyUrl);
        if (!openBrowser(info.apiKeyUrl))
            std::print("Could not open browser. Please visit: {}\n", info.apiKeyUrl);

        // Read the API key with masked input.
        auto const apiKey = askFreeText("Paste your API key:", /*masked=*/true);
        if (!apiKey.has_value() || apiKey->empty())
        {
            std::print(stderr, "No API key provided.\n");
            return EXIT_FAILURE;
        }

        // Validate the key.
        std::print("Validating API key...");
        if (!validateApiKey(providerName, *apiKey))
        {
            std::print(stderr, "\nAuthentication failed: invalid API key.\n");
            return EXIT_FAILURE;
        }
        std::print(" OK\n");

        // Save to config.
        setProviderApiKey(config, providerName, *apiKey);

        if (auto error = saveAgentConfig(config))
        {
            std::print(stderr, "Failed to save configuration: {}\n", *error);
            return EXIT_FAILURE;
        }

        std::print("Authenticated as {}. API key saved to ~/.config/endo/agent.yml\n", providerName);
        std::print("To select this provider, add to ~/.config/endo/init.endo:\n");
        std::print("  set_agent_provider \"{}\"\n", providerName);
        return EXIT_SUCCESS;
    }

    // ── OAuth Login Flow ─────────────────────────────────────────────────────

    auto runOAuthLoginFlow(std::string_view providerName) -> int
    {
        // Currently only Claude supports OAuth.
        if (providerName != "claude")
        {
            std::print(stderr, "OAuth login is not supported for {}.\n", providerName);
            return EXIT_FAILURE;
        }

        // Ask which account type.
        constexpr auto accountTypes = std::array {
            "Claude.ai (MAX/Pro subscription)"sv,
            "Anthropic Console (Teams/Enterprise)"sv,
        };
        auto const accountSel = askSingleSelect("Select account type:", accountTypes);
        if (!accountSel)
            return EXIT_FAILURE;

        auto const mode = (*accountSel == 1) ? OAuthMode::Console : OAuthMode::ClaudeAi;

        // Generate PKCE parameters.
        auto const pkce = generatePkce();

        // Start the local callback server.
        auto server = OAuthCallbackServer {};
        auto const portResult = server.start();

        auto redirectUri = std::string {};
        if (portResult.has_value())
        {
            redirectUri = std::format("http://localhost:{}/callback", *portResult);
        }
        else
        {
            // Fall back to manual code entry.
            redirectUri = "https://platform.claude.com/oauth/code/callback";
        }

        // Build and display the authorization URL.
        auto const authUrl = buildAuthorizeUrl(mode, pkce, redirectUri);

        std::print("\nOpening browser for authentication...\n");
        if (!openBrowser(authUrl))
            std::print("Could not open browser automatically.\n");

        std::print("\n  If the browser did not open, visit:\n  {}\n\n", authUrl);

        // Wait for the callback or manual entry.
        auto code = std::string {};
        auto state = std::string {};

        if (portResult.has_value())
        {
            std::print("Waiting for browser callback on http://localhost:{}/callback...\n", *portResult);
            std::print("(Or paste the authorization code manually and press Enter)\n> ");

            // Try the callback server with a timeout.
            auto const callbackResult = server.waitForCallback(std::chrono::seconds(120));
            if (callbackResult.has_value())
            {
                code = callbackResult->code;
                state = callbackResult->state;
                std::print("Received authorization callback.\n");
            }
            else
            {
                // Callback server timed out — ask for manual code entry.
                auto const manualCode = askFreeText("Callback timed out. Paste the authorization code:");
                if (!manualCode.has_value() || manualCode->empty())
                {
                    std::print(stderr, "No authorization code provided.\n");
                    return EXIT_FAILURE;
                }

                // The manual code may be in the form "code#state".
                if (auto const hashPos = manualCode->find('#'); hashPos != std::string::npos)
                {
                    code = manualCode->substr(0, hashPos);
                    state = manualCode->substr(hashPos + 1);
                }
                else
                {
                    code = *manualCode;
                    state = pkce.state;
                }
            }
        }
        else
        {
            // No callback server — manual entry only.
            auto const manualCode = askFreeText("Paste the authorization code (code#state):");
            if (!manualCode.has_value() || manualCode->empty())
            {
                std::print(stderr, "No authorization code provided.\n");
                return EXIT_FAILURE;
            }

            if (auto const hashPos = manualCode->find('#'); hashPos != std::string::npos)
            {
                code = manualCode->substr(0, hashPos);
                state = manualCode->substr(hashPos + 1);
            }
            else
            {
                code = *manualCode;
                state = pkce.state;
            }
        }

        server.close();

        if (code.empty())
        {
            std::print(stderr, "No authorization code received.\n");
            return EXIT_FAILURE;
        }

        // Exchange the code for tokens.
        std::print("Exchanging authorization code for tokens...");
        auto httpClient = http::HttpClient {};
        auto const exchangeResult = exchangeCode(httpClient, code, state, pkce.verifier, redirectUri, mode);

        if (!exchangeResult.has_value())
        {
            std::print(stderr, "\nToken exchange failed: {}\n", exchangeResult.error());
            return EXIT_FAILURE;
        }
        std::print(" OK\n");

        // Save OAuth credentials.
        auto store = loadOAuthStore();
        store.claude = *exchangeResult;

        if (auto error = saveOAuthStore(store))
        {
            std::print(stderr, "Failed to save OAuth credentials: {}\n", *error);
            return EXIT_FAILURE;
        }

        std::print("Login successful! OAuth credentials saved to {}\n", oauthStorePath().string());
        std::print("To select this provider, add to ~/.config/endo/init.endo:\n");
        std::print("  set_agent_provider \"claude\"\n");
        return EXIT_SUCCESS;
    }

    // ── Google OAuth Login Flow ─────────────────────────────────────────

    auto runGoogleOAuthLoginFlow() -> int
    {
        // Generate PKCE parameters.
        auto const pkce = generatePkce();

        // Start the local callback server.
        auto server = OAuthCallbackServer {};
        auto const portResult = server.start();

        if (!portResult.has_value())
        {
            std::print(stderr, "Failed to start callback server: {}\n", portResult.error());
            return EXIT_FAILURE;
        }

        // Google requires 127.0.0.1 (IP literal), not "localhost".
        auto const redirectUri = std::format("http://127.0.0.1:{}/oauth2callback", *portResult);

        // Build and display the authorization URL.
        auto const authUrl = buildGoogleAuthorizeUrl(pkce, redirectUri);

        std::print("\nOpening browser for Google authentication...\n");
        if (!openBrowser(authUrl))
            std::print("Could not open browser automatically.\n");

        std::print("\n  If the browser did not open, visit:\n  {}\n\n", authUrl);

        // Wait for the callback.
        std::print("Waiting for browser callback on {}...\n", redirectUri);

        auto const callbackResult = server.waitForCallback(std::chrono::seconds(120));
        server.close();

        if (!callbackResult.has_value())
        {
            std::print(stderr, "Callback timed out or failed.\n");
            return EXIT_FAILURE;
        }

        auto const& code = callbackResult->code;
        if (code.empty())
        {
            std::print(stderr, "No authorization code received.\n");
            return EXIT_FAILURE;
        }

        // Exchange the code for tokens.
        std::print("Exchanging authorization code for tokens...");
        auto httpClient = http::HttpClient {};
        auto const exchangeResult = exchangeGoogleCode(httpClient, code, pkce.verifier, redirectUri);

        if (!exchangeResult.has_value())
        {
            std::print(stderr, "\nToken exchange failed: {}\n", exchangeResult.error());
            return EXIT_FAILURE;
        }
        std::print(" OK\n");

        // Save OAuth credentials.
        auto store = loadOAuthStore();
        store.gemini = *exchangeResult;

        if (auto error = saveOAuthStore(store))
        {
            std::print(stderr, "Failed to save OAuth credentials: {}\n", *error);
            return EXIT_FAILURE;
        }

        std::print("Login successful! Google OAuth credentials saved to {}\n", oauthStorePath().string());
        std::print("To select this provider, add to ~/.config/endo/init.endo:\n");
        std::print("  set_agent_provider \"gemini\"\n");
        return EXIT_SUCCESS;
    }

    // ── GitHub Copilot Device Flow Login ──────────────────────────────

    auto runCopilotDeviceFlow() -> int
    {
        auto httpClient = http::HttpClient {};

        // Step 1: Request device code.
        std::print("Requesting device authorization...\n");
        auto const deviceCodeResult = requestGitHubDeviceCode(httpClient);
        if (!deviceCodeResult.has_value())
        {
            std::print(stderr, "Failed: {}\n", deviceCodeResult.error());
            return EXIT_FAILURE;
        }
        auto const& dc = *deviceCodeResult;

        // Step 2: Display user code and open browser.
        std::print("\n  Enter code: {}\n", dc.userCode);
        std::print("  at: {}\n\n", dc.verificationUri);
        (void) openBrowser(dc.verificationUri);

        // Step 3: Poll for completion.
        std::print("Waiting for authorization...");
        auto const ghTokenResult = pollGitHubDeviceAuth(httpClient, dc);
        if (!ghTokenResult.has_value())
        {
            std::print(stderr, "\n{}\n", ghTokenResult.error());
            return EXIT_FAILURE;
        }
        std::print(" OK\n");

        // Step 4: Validate by exchanging for Copilot session token.
        std::print("Validating Copilot access...");
        auto const sessionResult = exchangeCopilotToken(httpClient, *ghTokenResult);
        if (!sessionResult.has_value())
        {
            std::print(stderr, "\nCopilot token exchange failed: {}\n", sessionResult.error());
            std::print(stderr, "Your GitHub account may not have an active Copilot subscription.\n");
            return EXIT_FAILURE;
        }
        std::print(" OK\n");

        // Step 5: Save the long-lived GitHub token to OAuthStore.
        auto store = loadOAuthStore();
        store.copilot = OAuthCredentials {
            .accessToken = *ghTokenResult,
            .refreshToken = {},
            .expiresAt = 0, // GitHub tokens do not expire.
            .authMode = "github_device",
        };

        if (auto error = saveOAuthStore(store))
        {
            std::print(stderr, "Failed to save credentials: {}\n", *error);
            return EXIT_FAILURE;
        }

        std::print("Login successful! Copilot credentials saved to {}\n", oauthStorePath().string());
        std::print("To select this provider, add to ~/.config/endo/init.endo:\n");
        std::print("  set_agent_provider \"copilot\"\n");
        return EXIT_SUCCESS;
    }

} // namespace

auto runLoginCommand(std::string_view providerHint) -> int
{
    auto const providerName = providerHint.empty() ? promptProviderSelection() : std::string(providerHint);
    if (providerName.empty())
    {
        std::print(stderr, "No provider selected.\n");
        return EXIT_FAILURE;
    }

    auto const* info = findProvider(providerName);
    if (!info)
    {
        std::print(stderr, "Unknown provider: {}\n", providerName);
        std::print(stderr, "Available providers: claude, openai, gemini, copilot\n");
        return EXIT_FAILURE;
    }

    // Copilot uses OAuth device flow exclusively — no API key option.
    if (providerName == "copilot")
        return runCopilotDeviceFlow();

    // If this provider supports OAuth, offer the choice.
    if (info->supportsOAuth)
    {
        auto const oauthLabel = (providerName == "gemini") ? "OAuth (Google One AI Premium)"sv
                                                           : "OAuth (Claude MAX/Pro/Teams subscription)"sv;
        auto const authLabels = std::array { "API Key"sv, oauthLabel };
        auto const authSel =
            askSingleSelect(std::format("Select authentication method for {}:", info->label), authLabels);
        if (!authSel)
            return EXIT_FAILURE;
        if (*authSel == 1)
        {
            if (providerName == "gemini")
                return runGoogleOAuthLoginFlow();
            return runOAuthLoginFlow(providerName);
        }
        // index 0 = API Key, falls through.
    }

    return runApiKeyLoginFlow(providerName, *info);
}

auto runStatusCommand() -> int
{
    auto const config = loadAgentConfig();
    auto const oauthStore = loadOAuthStore();

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

    std::print("\n{}{:<16}{:<18}{}{}\n", bold, "Provider", "Status", "Source", reset);
    std::print("{}{:─<16}{:─<18}{:─<14}{}\n", dim, "", "", "", reset);

    auto const allProviders =
        std::array { "claude"sv, "openai"sv, "gemini"sv, "copilot"sv, "openai_compat"sv, "local"sv };

    for (auto const& provider: allProviders)
    {
        auto const authenticated = isProviderAuthenticatedFull(config, provider, oauthStore);
        auto const source = getAuthSource(config, provider, oauthStore);

        // Status: green for authenticated, red for not configured.
        auto const statusColor = authenticated ? green : red;
        auto const statusText = authenticated ? "authenticated"sv : "not configured"sv;

        // Source: dim yellow.
        auto const sourceColor = authenticated ? yellow : ""sv;
        auto const sourceText = authenticated ? std::string_view(source) : ""sv;

        std::print("{:<16}{}{:<18}{}{}{}{}\n",
                   provider,
                   statusColor,
                   statusText,
                   reset,
                   sourceColor,
                   sourceText,
                   reset);
    }
    std::print("\n{}Configure active provider in ~/.config/endo/init.endo:{}\n", dim, reset);
    std::print("{}  set_agent_provider \"name\"{}\n\n", dim, reset);
    return EXIT_SUCCESS;
}

auto runLogoutCommand(std::string_view providerHint) -> int
{
    auto config = loadAgentConfig();
    auto oauthStore = loadOAuthStore();

    auto const providerName = providerHint.empty() ? promptProviderSelection() : std::string(providerHint);
    if (providerName.empty())
        return EXIT_FAILURE;

    auto const* info = findProvider(providerName);
    if (!info)
    {
        std::print(stderr, "Unknown provider: {}\n", providerName);
        return EXIT_FAILURE;
    }

    auto removedSomething = false;

    // Remove OAuth credentials.
    if (providerName == "claude" && oauthStore.claude.has_value())
    {
        oauthStore.claude.reset();
        if (auto error = saveOAuthStore(oauthStore))
            std::print(stderr, "Warning: failed to save OAuth store: {}\n", *error);
        else
            removedSomething = true;
    }
    else if (providerName == "openai" && oauthStore.openai.has_value())
    {
        oauthStore.openai.reset();
        if (auto error = saveOAuthStore(oauthStore))
            std::print(stderr, "Warning: failed to save OAuth store: {}\n", *error);
        else
            removedSomething = true;
    }
    else if (providerName == "gemini" && oauthStore.gemini.has_value())
    {
        oauthStore.gemini.reset();
        if (auto error = saveOAuthStore(oauthStore))
            std::print(stderr, "Warning: failed to save OAuth store: {}\n", *error);
        else
            removedSomething = true;
    }
    else if (providerName == "copilot" && oauthStore.copilot.has_value())
    {
        oauthStore.copilot.reset();
        if (auto error = saveOAuthStore(oauthStore))
            std::print(stderr, "Warning: failed to save OAuth store: {}\n", *error);
        else
            removedSomething = true;
    }

    // Also remove stored API key.
    if (isProviderAuthenticated(config, providerName))
    {
        clearProviderApiKey(config, providerName);
        if (auto error = saveAgentConfig(config))
        {
            std::print(stderr, "Failed to save configuration: {}\n", *error);
            return EXIT_FAILURE;
        }
        removedSomething = true;
    }

    if (removedSomething)
        std::print("Removed stored credentials for {}.\n", providerName);
    else
        std::print("No credentials found for {}.\n", providerName);

    return EXIT_SUCCESS;
}

} // namespace endo::agent
