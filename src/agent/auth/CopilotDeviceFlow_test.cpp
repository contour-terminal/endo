// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>

#include <agent/auth/CopilotDeviceFlow.hpp>

using namespace endo::agent;

// =============================================================================
// CopilotSessionToken expiry tests
// =============================================================================

TEST_CASE("CopilotDeviceFlow.empty_token_is_expired")
{
    auto const token = CopilotSessionToken {};
    CHECK(isCopilotTokenExpired(token));
}

TEST_CASE("CopilotDeviceFlow.expired_token_detected")
{
    auto const now =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();

    // Token that expired 10 minutes ago.
    auto const token = CopilotSessionToken {
        .token = "test-token",
        .expiresAt = now - 600,
    };
    CHECK(isCopilotTokenExpired(token));
}

TEST_CASE("CopilotDeviceFlow.token_within_buffer_is_expired")
{
    auto const now =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();

    // Token that expires in 1 minute (within 2-minute buffer).
    auto const token = CopilotSessionToken {
        .token = "test-token",
        .expiresAt = now + 60,
    };
    CHECK(isCopilotTokenExpired(token));
}

TEST_CASE("CopilotDeviceFlow.valid_token_not_expired")
{
    auto const now =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();

    // Token that expires in 10 minutes (outside 2-minute buffer).
    auto const token = CopilotSessionToken {
        .token = "test-token",
        .expiresAt = now + 600,
    };
    CHECK_FALSE(isCopilotTokenExpired(token));
}

TEST_CASE("CopilotDeviceFlow.token_at_buffer_boundary_is_expired")
{
    auto const now =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();

    // Token that expires in exactly 2 minutes (at boundary).
    auto const token = CopilotSessionToken {
        .token = "test-token",
        .expiresAt = now + 120,
    };
    CHECK(isCopilotTokenExpired(token));
}
