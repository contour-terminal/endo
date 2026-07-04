// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// A minimal async HTTP/1.1 server built on the coroutine socket layer. Parses a
/// request off an @c ISocket, invokes a handler, and writes the response back —
/// the C++ core behind the `httpServe` scripting builtin. Handlers are plain
/// `std::function`s so the layer is fully testable without the VM; the builtin
/// supplies a handler that dispatches into a script function.

#include <expected>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <coro/Task.hpp>
#include <net/IListener.hpp>
#include <net/ISocket.hpp>
#include <net/IoResult.hpp>

namespace endo::net
{

/// A parsed HTTP request (the subset the server exposes to handlers).
struct HttpRequest
{
    std::string method;                                       ///< "GET", "POST", …
    std::string path;                                         ///< Request target, e.g. "/index".
    std::string version;                                      ///< "HTTP/1.1".
    std::vector<std::pair<std::string, std::string>> headers; ///< Header name/value pairs.
    std::string body;                                         ///< Request body (may be empty).

    /// Looks up a header value (case-insensitive name match).
    /// @param name The header name to find.
    /// @return The header value, or "" if absent.
    [[nodiscard]] std::string header(std::string_view name) const;
};

/// An HTTP response a handler produces.
struct HttpResponse
{
    int status = 200;                                         ///< HTTP status code.
    std::string reason = "OK";                                ///< Reason phrase.
    std::vector<std::pair<std::string, std::string>> headers; ///< Extra headers (Content-Length is added).
    std::string body;                                         ///< Response body.

    /// @param text The body text.
    /// @return A 200 OK response with @p text as a text/plain body.
    [[nodiscard]] static HttpResponse ok(std::string text);

    /// @param status The status code.
    /// @param text The body text.
    /// @return A response with the given status and a text/plain body.
    [[nodiscard]] static HttpResponse withStatus(int status, std::string text);
};

/// A request handler: maps a request to a response. May be a native lambda or, via
/// the builtin, a dispatcher into a script function.
using HttpHandler = std::function<HttpResponse(HttpRequest const&)>;

/// Serves connections from @p listener until it is closed, dispatching each
/// request to @p handler. Each accepted connection is handled in sequence on the
/// single runtime thread (a slow handler stalls the loop — acceptable for the
/// single-threaded scripting model). Returns when the listener is closed /
/// cancelled. The listener and its accepted sockets already carry the runtime that
/// drives their I/O, so it need not be passed separately.
/// @param listener The bound listener to accept from (not owned).
/// @param handler The request handler.
/// @return A task that completes when serving stops.
[[nodiscard]] endo::coro::Task<void> serve(IListener* listener, HttpHandler handler);

/// Reads and parses a single HTTP/1.1 request from @p socket (handling a
/// Content-Length body). Exposed for testing the framing directly.
/// @param socket The connection to read from (not owned).
/// @return A task resolving to the parsed request, or a @c NetError on failure
///         (including EOF before a complete request).
[[nodiscard]] endo::coro::Task<std::expected<HttpRequest, NetError>> readRequest(ISocket* socket);

/// Writes @p response to @p socket as an HTTP/1.1 response. Exposed for testing.
/// @param socket The connection to write to (not owned).
/// @param response The response to serialize (taken by value: a coroutine must not
///        hold a reference parameter across a suspend point).
/// @return A task resolving to the bytes written, or a @c NetError on failure.
[[nodiscard]] endo::coro::Task<IoResult> writeResponse(ISocket* socket, HttpResponse response);

} // namespace endo::net
