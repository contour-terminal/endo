// SPDX-License-Identifier: Apache-2.0
#include "CopilotDeviceFlow.hpp"

#include <http/HttpClient.hpp>

#include <chrono>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

namespace endo::agent
{

namespace
{
    // ── GitHub Copilot OAuth Constants ───────────────────────────────────────

    /// Public OAuth application client ID for GitHub Copilot (same across all integrations).
    constexpr auto GitHubClientId = "Iv1.b507a08c87ecfe98";

    /// GitHub device code request endpoint.
    constexpr auto GitHubDeviceCodeUrl = "https://github.com/login/device/code";

    /// GitHub OAuth access token endpoint (for device flow polling).
    constexpr auto GitHubDeviceTokenUrl = "https://github.com/login/oauth/access_token";

    /// Copilot session token exchange endpoint.
    constexpr auto CopilotTokenUrl = "https://api.github.com/copilot_internal/v2/token";

    /// 2-minute buffer before token expiry to trigger refresh.
    constexpr int64_t ExpiryBufferSeconds = 2 * 60;

    /// Returns the current time as seconds since Unix epoch.
    auto currentTimeSeconds() -> int64_t
    {
        return std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    }

} // namespace

auto isCopilotTokenExpired(CopilotSessionToken const& sessionToken) -> bool
{
    if (sessionToken.token.empty())
        return true;
    return currentTimeSeconds() >= sessionToken.expiresAt - ExpiryBufferSeconds;
}

auto requestGitHubDeviceCode(http::HttpClient const& httpClient)
    -> std::expected<GitHubDeviceCode, std::string>
{
    auto const body = std::string("client_id=") + GitHubClientId + "&scope=read:user";

    auto request = http::HttpRequest {};
    request.url = GitHubDeviceCodeUrl;
    request.method = http::HttpMethod::Post;
    request.headers = { "Content-Type: application/x-www-form-urlencoded", "Accept: application/json" };
    request.body = body;
    request.timeout = std::chrono::seconds(30);

    auto const result = httpClient.execute(request);
    if (!result.has_value())
        return std::unexpected(std::string("Network error: ") + result.error().message);

    if (result->statusCode != 200)
        return std::unexpected(std::string("Device code request failed (HTTP ")
                               + std::to_string(result->statusCode) + "): " + result->body.substr(0, 500));

    try
    {
        auto const json = nlohmann::json::parse(result->body);

        return GitHubDeviceCode {
            .deviceCode = json.at("device_code").get<std::string>(),
            .userCode = json.at("user_code").get<std::string>(),
            .verificationUri = json.at("verification_uri").get<std::string>(),
            .expiresIn = json.value("expires_in", 900),
            .interval = json.value("interval", 5),
        };
    }
    catch (nlohmann::json::exception const& e)
    {
        return std::unexpected(std::string("Failed to parse device code response: ") + e.what());
    }
}

auto pollGitHubDeviceAuth(http::HttpClient const& httpClient, GitHubDeviceCode const& deviceCode)
    -> std::expected<std::string, std::string>
{
    auto const body = std::string("client_id=") + GitHubClientId + "&device_code=" + deviceCode.deviceCode
                      + "&grant_type=urn:ietf:params:oauth:grant-type:device_code";

    auto const deadline = currentTimeSeconds() + deviceCode.expiresIn;
    auto interval = deviceCode.interval;

    while (currentTimeSeconds() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::seconds(interval));

        auto request = http::HttpRequest {};
        request.url = GitHubDeviceTokenUrl;
        request.method = http::HttpMethod::Post;
        request.headers = { "Content-Type: application/x-www-form-urlencoded", "Accept: application/json" };
        request.body = body;
        request.timeout = std::chrono::seconds(30);

        auto const result = httpClient.execute(request);
        if (!result.has_value())
            continue; // Transient network error — retry.

        try
        {
            auto const json = nlohmann::json::parse(result->body);

            // Check for access_token first (success case).
            if (json.contains("access_token"))
                return json["access_token"].get<std::string>();

            // Handle error codes per RFC 8628.
            auto const error = json.value("error", std::string {});

            if (error == "authorization_pending")
                continue; // User hasn't authorized yet — keep polling.

            if (error == "slow_down")
            {
                interval += 5; // Increase polling interval as requested.
                continue;
            }

            if (error == "expired_token")
                return std::unexpected(std::string("Device code expired. Please try again."));

            if (error == "access_denied")
                return std::unexpected(std::string("Authorization denied by user."));

            // Unknown error.
            auto const description = json.value("error_description", error);
            return std::unexpected(std::string("Authorization failed: ") + description);
        }
        catch (nlohmann::json::exception const&)
        {
            continue; // Malformed response — retry.
        }
    }

    return std::unexpected(std::string("Device code expired (timeout). Please try again."));
}

auto exchangeCopilotToken(http::HttpClient const& httpClient, std::string_view githubToken)
    -> std::expected<CopilotSessionToken, std::string>
{
    auto request = http::HttpRequest {};
    request.url = CopilotTokenUrl;
    request.method = http::HttpMethod::Get;
    request.headers = { std::string("Authorization: Bearer ") + std::string(githubToken),
                        "Accept: application/json" };
    request.timeout = std::chrono::seconds(30);

    auto const result = httpClient.execute(request);
    if (!result.has_value())
        return std::unexpected(std::string("Network error: ") + result.error().message);

    if (result->statusCode == 401)
        return std::unexpected(std::string("GitHub token is invalid or expired."));

    if (result->statusCode != 200)
        return std::unexpected(std::string("Copilot token exchange failed (HTTP ")
                               + std::to_string(result->statusCode) + "): " + result->body.substr(0, 500));

    try
    {
        auto const json = nlohmann::json::parse(result->body);

        auto const token = json.at("token").get<std::string>();
        auto const expiresAt = json.at("expires_at").get<int64_t>();

        return CopilotSessionToken {
            .token = token,
            .expiresAt = expiresAt,
        };
    }
    catch (nlohmann::json::exception const& e)
    {
        return std::unexpected(std::string("Failed to parse Copilot token response: ") + e.what());
    }
}

} // namespace endo::agent
