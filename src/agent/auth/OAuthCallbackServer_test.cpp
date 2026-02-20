// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <agent/auth/OAuthCallbackServer.hpp>

#if !defined(_WIN32)
    #include <sys/socket.h>

    #include <unistd.h>

    #include <arpa/inet.h>
    #include <netinet/in.h>
#endif

#include <cstring>
#include <string>
#include <thread>

using namespace endo::agent;

TEST_CASE("OAuthCallbackServer.start_binds_to_ephemeral_port")
{
    auto server = OAuthCallbackServer {};
    auto const result = server.start();
    REQUIRE(result.has_value());

    auto const port = *result;
    CHECK(port > 0);
    CHECK(port <= 65535);

    server.close();
}

TEST_CASE("OAuthCallbackServer.timeout_returns_error")
{
    auto server = OAuthCallbackServer {};
    auto const portResult = server.start();
    REQUIRE(portResult.has_value());

    // Wait with a very short timeout — no client will connect.
    auto const result = server.waitForCallback(std::chrono::seconds(1));
    CHECK_FALSE(result.has_value());
    CHECK(result.error().find("Timed out") != std::string::npos);

    server.close();
}

#if !defined(_WIN32)
TEST_CASE("OAuthCallbackServer.receives_callback_with_code_and_state")
{
    auto server = OAuthCallbackServer {};
    auto const portResult = server.start();
    REQUIRE(portResult.has_value());
    auto const port = *portResult;

    // Simulate a browser redirect in a separate thread.
    auto clientThread = std::thread([port]() {
        // Small delay to ensure server is waiting.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        auto const fd = socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(fd >= 0);

        auto addr = sockaddr_in {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        auto const connected = connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        REQUIRE(connected == 0);

        auto const request = std::string("GET /?code=test-auth-code&state=test-state HTTP/1.1\r\n"
                                         "Host: 127.0.0.1\r\n\r\n");
        send(fd, request.data(), request.size(), 0);

        // Read the response (HTML success page).
        char buf[4096];
        recv(fd, buf, sizeof(buf), 0);

        close(fd);
    });

    auto const result = server.waitForCallback(std::chrono::seconds(5));
    clientThread.join();

    REQUIRE(result.has_value());
    CHECK(result->code == "test-auth-code");
    CHECK(result->state == "test-state");

    server.close();
}
#endif

TEST_CASE("OAuthCallbackServer.move_semantics")
{
    auto server1 = OAuthCallbackServer {};
    auto const portResult = server1.start();
    REQUIRE(portResult.has_value());

    // Move construct.
    auto server2 = std::move(server1);

    // Original should be safe to destroy (no double-close).
    // NOLINTNEXTLINE(bugprone-use-after-move)
    server1.close(); // Should be a no-op.

    // Moved-to should still be valid.
    server2.close();
}
