// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

#include <agent/auth/OAuthFlow.hpp>

using namespace endo::agent;

TEST_CASE("OAuthFlow.generatePkce_produces_valid_parameters")
{
    auto const pkce = generatePkce();

    // Verifier should be base64url-encoded (86 chars for 64 bytes).
    CHECK(!pkce.verifier.empty());
    CHECK(pkce.verifier.size() >= 43); // At least 32 bytes encoded.

    // Challenge should be base64url-encoded SHA-256 (43 chars for 32 bytes).
    CHECK(!pkce.challenge.empty());
    CHECK(pkce.challenge.size() == 43); // SHA-256 = 32 bytes -> 43 base64url chars.

    // State should be non-empty.
    CHECK(!pkce.state.empty());

    // Verifier should only contain base64url characters.
    for (auto const ch: pkce.verifier)
        CHECK((std::isalnum(ch) || ch == '-' || ch == '_'));

    // Challenge should only contain base64url characters.
    for (auto const ch: pkce.challenge)
        CHECK((std::isalnum(ch) || ch == '-' || ch == '_'));
}

TEST_CASE("OAuthFlow.generatePkce_produces_unique_values")
{
    auto const pkce1 = generatePkce();
    auto const pkce2 = generatePkce();

    // Two calls should produce different verifiers (random).
    CHECK(pkce1.verifier != pkce2.verifier);
    CHECK(pkce1.challenge != pkce2.challenge);
    CHECK(pkce1.state != pkce2.state);
}

TEST_CASE("OAuthFlow.buildAuthorizeUrl_ClaudeAi")
{
    auto const pkce = PkceParams {
        .verifier = "test-verifier",
        .challenge = "test-challenge",
        .state = "test-state",
    };

    auto const url = buildAuthorizeUrl(OAuthMode::ClaudeAi, pkce, "http://localhost:12345/callback");

    CHECK(url.find("https://claude.ai/oauth/authorize") != std::string::npos);
    // code=true must be the first query parameter.
    CHECK(url.find("?code=true&") != std::string::npos);
    CHECK(url.find("client_id=") != std::string::npos);
    CHECK(url.find("response_type=code") != std::string::npos);
    CHECK(url.find("code_challenge=test-challenge") != std::string::npos);
    CHECK(url.find("code_challenge_method=S256") != std::string::npos);
    CHECK(url.find("state=test-state") != std::string::npos);
    CHECK(url.find("scope=") != std::string::npos);
    // Scopes must include claude_code session scope.
    CHECK(url.find("user%3Asessions%3Aclaude_code") != std::string::npos);
}

TEST_CASE("OAuthFlow.buildAuthorizeUrl_Console")
{
    auto const pkce = PkceParams {
        .verifier = "v",
        .challenge = "c",
        .state = "s",
    };

    auto const url = buildAuthorizeUrl(OAuthMode::Console, pkce, "http://localhost:54321/callback");

    CHECK(url.find("https://console.anthropic.com/oauth/authorize") != std::string::npos);
    // code=true must be the first query parameter.
    CHECK(url.find("?code=true&") != std::string::npos);
    // Console scopes should include org:create_api_key and claude_code session scope.
    CHECK(url.find("scope=") != std::string::npos);
    CHECK(url.find("user%3Asessions%3Aclaude_code") != std::string::npos);
}

TEST_CASE("OAuthFlow.buildGoogleAuthorizeUrl_contains_valid_credentials")
{
    auto const pkce = PkceParams {
        .verifier = "test-verifier",
        .challenge = "test-challenge",
        .state = "test-state",
    };

    auto const url = buildGoogleAuthorizeUrl(pkce, "http://localhost:12345/callback");

    // Validates that the base64-decoded Google client ID has the expected format.
    CHECK(url.find("https://accounts.google.com/o/oauth2/v2/auth") != std::string::npos);
    CHECK(url.find("client_id=") != std::string::npos);
    CHECK(url.find(".apps.googleusercontent.com") != std::string::npos);
    CHECK(url.find("response_type=code") != std::string::npos);
    CHECK(url.find("code_challenge=test-challenge") != std::string::npos);
    CHECK(url.find("code_challenge_method=S256") != std::string::npos);
    CHECK(url.find("state=test-state") != std::string::npos);
    CHECK(url.find("scope=") != std::string::npos);
    CHECK(url.find("access_type=offline") != std::string::npos);
    CHECK(url.find("prompt=consent") != std::string::npos);
}

TEST_CASE("OAuthFlow.isOAuthToken_detects_prefix")
{
    CHECK(isOAuthToken("sk-ant-oat01-abc123"));
    CHECK(isOAuthToken("sk-ant-oat02-xyz789-long-token-here"));
    CHECK_FALSE(isOAuthToken("sk-ant-api03-xyz789"));
    CHECK_FALSE(isOAuthToken("some-random-key"));
    CHECK_FALSE(isOAuthToken(""));
}

TEST_CASE("OAuthFlow.isTokenExpired_detects_expiry")
{
    auto const now = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();

    // Token that expired 10 minutes ago.
    auto expired = OAuthCredentials {};
    expired.expiresAt = now - 10 * 60 * 1000;
    CHECK(isTokenExpired(expired));

    // Token that expires in 1 minute (within 5-minute buffer).
    auto almostExpired = OAuthCredentials {};
    almostExpired.expiresAt = now + 1 * 60 * 1000;
    CHECK(isTokenExpired(almostExpired));

    // Token that expires in 10 minutes (outside buffer).
    auto valid = OAuthCredentials {};
    valid.expiresAt = now + 10 * 60 * 1000;
    CHECK_FALSE(isTokenExpired(valid));

    // Token that expires in exactly 5 minutes (boundary).
    auto boundary = OAuthCredentials {};
    boundary.expiresAt = now + 5 * 60 * 1000;
    CHECK(isTokenExpired(boundary));
}

TEST_CASE("OAuthFlow.credential_store_round_trip")
{
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-oauth-test";
    std::filesystem::create_directories(tmpDir);
    auto const tmpFile = tmpDir / "test-oauth.yaml";

    // Clean up any previous test file.
    std::filesystem::remove(tmpFile);

    auto store = OAuthStore {};
    store.claude = OAuthCredentials {
        .accessToken = "sk-ant-oat01-test-access",
        .refreshToken = "sk-ant-test-refresh",
        .expiresAt = 1750000000000,
        .authMode = "claude_ai",
    };

    // Save.
    auto const saveError = saveOAuthStore(store, tmpFile);
    CHECK_FALSE(saveError.has_value());
    CHECK(std::filesystem::exists(tmpFile));

    // Verify restricted permissions on Unix.
#if !defined(_WIN32)
    auto const perms = std::filesystem::status(tmpFile).permissions();
    CHECK((perms & std::filesystem::perms::owner_read) != std::filesystem::perms::none);
    CHECK((perms & std::filesystem::perms::owner_write) != std::filesystem::perms::none);
    CHECK((perms & std::filesystem::perms::group_read) == std::filesystem::perms::none);
    CHECK((perms & std::filesystem::perms::others_read) == std::filesystem::perms::none);
#endif

    // Load.
    auto const loaded = loadOAuthStore(tmpFile);
    REQUIRE(loaded.claude.has_value());
    CHECK(loaded.claude->accessToken == "sk-ant-oat01-test-access");
    CHECK(loaded.claude->refreshToken == "sk-ant-test-refresh");
    CHECK(loaded.claude->expiresAt == 1750000000000);
    CHECK(loaded.claude->authMode == "claude_ai");
    CHECK_FALSE(loaded.openai.has_value());
    CHECK_FALSE(loaded.gemini.has_value());

    // Clean up.
    std::filesystem::remove_all(tmpDir);
}

TEST_CASE("OAuthFlow.loadOAuthStore_missing_file_returns_empty")
{
    auto const store = loadOAuthStore("/nonexistent/path/oauth.yaml");
    CHECK_FALSE(store.claude.has_value());
    CHECK_FALSE(store.openai.has_value());
    CHECK_FALSE(store.gemini.has_value());
}

TEST_CASE("OAuthFlow.credential_store_multiple_providers")
{
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-oauth-test-multi";
    std::filesystem::create_directories(tmpDir);
    auto const tmpFile = tmpDir / "multi-oauth.yaml";
    std::filesystem::remove(tmpFile);

    auto store = OAuthStore {};
    store.claude = OAuthCredentials {
        .accessToken = "claude-token",
        .refreshToken = "claude-refresh",
        .expiresAt = 1000,
        .authMode = "console",
    };
    store.openai = OAuthCredentials {
        .accessToken = "openai-token",
        .refreshToken = "openai-refresh",
        .expiresAt = 2000,
    };

    auto const saveError = saveOAuthStore(store, tmpFile);
    CHECK_FALSE(saveError.has_value());

    auto const loaded = loadOAuthStore(tmpFile);
    REQUIRE(loaded.claude.has_value());
    CHECK(loaded.claude->accessToken == "claude-token");
    CHECK(loaded.claude->authMode == "console");

    REQUIRE(loaded.openai.has_value());
    CHECK(loaded.openai->accessToken == "openai-token");

    CHECK_FALSE(loaded.gemini.has_value());

    std::filesystem::remove_all(tmpDir);
}
