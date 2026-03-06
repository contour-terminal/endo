// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>

namespace endo::http
{
class HttpClient;
}

namespace endo::agent
{

/// Provider-independent OAuth credentials.
struct OAuthCredentials
{
    std::string accessToken;  ///< The OAuth access token.
    std::string refreshToken; ///< Token used to obtain a new access token.
    int64_t expiresAt = 0;    ///< Expiry as Unix timestamp in milliseconds.
    std::string authMode;     ///< Provider-specific mode (e.g. "claude_ai", "console").
};

/// All stored OAuth credentials, keyed by provider name.
struct OAuthStore
{
    std::optional<OAuthCredentials> claude;
    std::optional<OAuthCredentials> openai;
    std::optional<OAuthCredentials> gemini;
    std::optional<OAuthCredentials> copilot;
};

/// PKCE verifier + S256 challenge pair for the OAuth authorization code flow.
struct PkceParams
{
    std::string verifier;  ///< 64 random bytes, base64url-encoded.
    std::string challenge; ///< base64url(SHA-256(verifier)).
    std::string state;     ///< Random state parameter for CSRF protection.
};

/// OAuth authorization mode for Anthropic Claude.
enum class OAuthMode // NOLINT(performance-enum-size)
{
    ClaudeAi, ///< Claude.ai subscription (MAX/Pro).
    Console,  ///< Anthropic Console (Teams/Enterprise).
};

/// Checks whether a token looks like an OAuth token (by prefix).
/// @param token The token string to inspect.
/// @return true if the token has an OAuth prefix (e.g. "sk-ant-oat").
[[nodiscard]] auto isOAuthToken(std::string_view token) -> bool;

/// Checks whether an OAuth access token is expired or about to expire (5-minute buffer).
/// @param creds The OAuth credentials to check.
/// @return true if the token is expired or will expire within 5 minutes.
[[nodiscard]] auto isTokenExpired(OAuthCredentials const& creds) -> bool;

/// Generates a PKCE verifier, S256 challenge, and random state parameter.
[[nodiscard]] auto generatePkce() -> PkceParams;

/// Builds the OAuth authorization URL for the given mode.
/// @param mode   Whether to use claude.ai or console.anthropic.com.
/// @param pkce   The PKCE parameters (challenge and state are embedded in the URL).
/// @param redirectUri The redirect URI (localhost callback or manual redirect).
/// @return The fully-formed authorization URL.
[[nodiscard]] auto buildAuthorizeUrl(OAuthMode mode, PkceParams const& pkce, std::string_view redirectUri)
    -> std::string;

/// Exchanges an authorization code for OAuth tokens.
/// @param httpClient HTTP client to use for the token endpoint request.
/// @param code       The authorization code received from the redirect.
/// @param state      The state parameter from the redirect (for CSRF verification).
/// @param verifier   The PKCE code verifier generated during the authorization request.
/// @param redirectUri The redirect URI used in the authorization request.
/// @param mode       The OAuth mode (used to set authMode on the returned credentials).
/// @return OAuth credentials on success, or an error message.
[[nodiscard]] auto exchangeCode(http::HttpClient const& httpClient,
                                std::string_view code,
                                std::string_view state,
                                std::string_view verifier,
                                std::string_view redirectUri,
                                OAuthMode mode) -> std::expected<OAuthCredentials, std::string>;

/// Refreshes an OAuth access token using the refresh token.
/// @param httpClient    HTTP client to use for the token endpoint request.
/// @param refreshToken  The refresh token from a previous token exchange.
/// @return Updated OAuth credentials on success, or an error message.
[[nodiscard]] auto refreshOAuthToken(http::HttpClient const& httpClient, std::string_view refreshToken)
    -> std::expected<OAuthCredentials, std::string>;

/// Builds the Google OAuth authorization URL for Google One AI Premium.
/// @param pkce        The PKCE parameters (challenge and state are embedded in the URL).
/// @param redirectUri The redirect URI (127.0.0.1 callback).
/// @return The fully-formed Google authorization URL.
[[nodiscard]] auto buildGoogleAuthorizeUrl(PkceParams const& pkce, std::string_view redirectUri)
    -> std::string;

/// Exchanges a Google authorization code for OAuth tokens.
/// @param httpClient  HTTP client to use for the token endpoint request.
/// @param code        The authorization code received from the redirect.
/// @param verifier    The PKCE code verifier generated during the authorization request.
/// @param redirectUri The redirect URI used in the authorization request.
/// @return OAuth credentials on success, or an error message.
[[nodiscard]] auto exchangeGoogleCode(http::HttpClient const& httpClient,
                                      std::string_view code,
                                      std::string_view verifier,
                                      std::string_view redirectUri)
    -> std::expected<OAuthCredentials, std::string>;

/// Refreshes a Google OAuth access token using the refresh token.
/// @param httpClient    HTTP client to use for the token endpoint request.
/// @param refreshToken  The refresh token from a previous token exchange.
/// @return Updated OAuth credentials on success, or an error message.
///         Google may omit the refresh_token in the response; the caller must preserve
///         the original refresh token if the returned one is empty.
[[nodiscard]] auto refreshGoogleOAuthToken(http::HttpClient const& httpClient, std::string_view refreshToken)
    -> std::expected<OAuthCredentials, std::string>;

/// Returns the default path for the OAuth credentials file (~/.config/endo/agent-oauth.yaml).
[[nodiscard]] auto oauthStorePath() -> std::filesystem::path;

/// Loads the OAuth credential store from a YAML file.
/// @param path Path to the YAML file.
/// @return The loaded store (missing providers have std::nullopt).
[[nodiscard]] auto loadOAuthStore(std::filesystem::path const& path) -> OAuthStore;

/// Loads the OAuth credential store from the default path.
[[nodiscard]] auto loadOAuthStore() -> OAuthStore;

/// Saves the OAuth credential store to a YAML file with restricted permissions (0600).
/// @param store The credentials to save.
/// @param path  Target file path.
/// @return std::nullopt on success, or an error message.
[[nodiscard]] auto saveOAuthStore(OAuthStore const& store, std::filesystem::path const& path)
    -> std::optional<std::string>;

/// Saves the OAuth credential store to the default path.
/// @param store The credentials to save.
/// @return std::nullopt on success, or an error message.
[[nodiscard]] auto saveOAuthStore(OAuthStore const& store) -> std::optional<std::string>;

} // namespace endo::agent
