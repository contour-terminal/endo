// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <expected>
#include <string>

namespace endo::http
{
class HttpClient;
}

namespace endo::agent
{

/// Response from the GitHub device code endpoint (RFC 8628).
struct GitHubDeviceCode
{
    std::string deviceCode;      ///< Device verification code sent to the token endpoint.
    std::string userCode;        ///< Short code displayed to the user (e.g. "ABCD-1234").
    std::string verificationUri; ///< URL the user visits to enter the code.
    int expiresIn = 0;           ///< Seconds until the device code expires.
    int interval = 5;            ///< Minimum polling interval in seconds.
};

/// Short-lived Copilot session token with expiry tracking.
struct CopilotSessionToken
{
    std::string token; ///< The Copilot API bearer token.
    int64_t expiresAt; ///< Expiry as Unix timestamp in seconds.
};

/// Checks whether a Copilot session token is expired or about to expire (2-minute buffer).
/// @param sessionToken The session token to check.
/// @return true if the token is expired or will expire within 2 minutes.
[[nodiscard]] auto isCopilotTokenExpired(CopilotSessionToken const& sessionToken) -> bool;

/// Requests a device code from GitHub for the OAuth device flow.
/// @param httpClient HTTP client for making the request.
/// @return The device code response, or an error message.
[[nodiscard]] auto requestGitHubDeviceCode(http::HttpClient const& httpClient)
    -> std::expected<GitHubDeviceCode, std::string>;

/// Polls GitHub for the OAuth access token using the device flow.
///
/// Blocks until the user authorizes the application or the device code expires.
/// Respects the polling interval and handles slow_down responses.
/// @param httpClient HTTP client for making requests.
/// @param deviceCode The device code response from requestGitHubDeviceCode().
/// @return The long-lived GitHub access token (ghu_ prefix), or an error message.
[[nodiscard]] auto pollGitHubDeviceAuth(http::HttpClient const& httpClient,
                                        GitHubDeviceCode const& deviceCode)
    -> std::expected<std::string, std::string>;

/// Exchanges a GitHub access token for a short-lived Copilot session token.
/// @param httpClient  HTTP client for making the request.
/// @param githubToken The GitHub OAuth access token.
/// @return The Copilot session token with expiry, or an error message.
[[nodiscard]] auto exchangeCopilotToken(http::HttpClient const& httpClient, std::string_view githubToken)
    -> std::expected<CopilotSessionToken, std::string>;

} // namespace endo::agent
