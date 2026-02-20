// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <expected>
#include <string>

namespace endo::agent
{

/// Result from a successful OAuth browser redirect callback.
struct OAuthCallback
{
    std::string code;  ///< The authorization code.
    std::string state; ///< The state parameter (for CSRF verification).
};

/// Minimal localhost HTTP server for receiving the OAuth browser redirect.
///
/// Starts listening on 127.0.0.1 with an OS-assigned ephemeral port,
/// waits for a single incoming connection with the authorization code,
/// sends an HTML success page to the browser, and shuts down.
class OAuthCallbackServer
{
  public:
    OAuthCallbackServer() = default;
    ~OAuthCallbackServer();

    OAuthCallbackServer(OAuthCallbackServer const&) = delete;
    OAuthCallbackServer& operator=(OAuthCallbackServer const&) = delete;
    OAuthCallbackServer(OAuthCallbackServer&&) noexcept;
    OAuthCallbackServer& operator=(OAuthCallbackServer&&) noexcept;

    /// Starts listening on 127.0.0.1 with an OS-assigned port.
    /// @return The port number on success, or an error message.
    [[nodiscard]] auto start() -> std::expected<uint16_t, std::string>;

    /// Blocks until the browser redirect arrives or the timeout expires.
    /// Extracts the `code` and `state` query parameters from the request URL,
    /// sends an HTML success page to the browser, then closes the connection.
    /// @param timeout Maximum time to wait for the callback.
    /// @return The parsed callback on success, or an error message.
    [[nodiscard]] auto waitForCallback(std::chrono::seconds timeout = std::chrono::seconds(120))
        -> std::expected<OAuthCallback, std::string>;

    /// Closes the listening socket if still open.
    void close();

  private:
    int _listenFd = -1;
};

} // namespace endo::agent
