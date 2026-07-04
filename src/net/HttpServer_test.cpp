// SPDX-License-Identifier: Apache-2.0
#include <tui/runtime/PollEventSource.hpp>
#include <tui/runtime/TuiRuntime.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <string>

#include <coro/Task.hpp>
#include <coro/WhenAll.hpp>
#include <coro/WhenAny.hpp>
#include <net/HttpServer.hpp>
#include <net/Sockets.hpp>

using endo::coro::Task;
using endo::net::HttpRequest;
using endo::net::HttpResponse;
using tui::runtime::PollEventSource;
using tui::runtime::TuiRuntime;

namespace
{

/// Serves exactly one request with a path-echoing handler, then stops.
Task<void> serveOne(TuiRuntime* runtime, endo::net::IListener* listener)
{
    auto accepted = co_await listener->accept();
    if (!accepted.has_value())
        co_return;
    auto conn = std::move(*accepted);

    auto request = co_await endo::net::readRequest(conn.get());
    if (!request.has_value())
        co_return;

    auto response = HttpResponse::ok("you asked for " + request->path);
    static_cast<void>(co_await endo::net::writeResponse(conn.get(), std::move(response)));
}

/// Connects, sends a GET, and reads the full response into @p out.
Task<void> httpGet(TuiRuntime* runtime, std::uint16_t port, std::string* out)
{
    auto connected = co_await endo::net::connect(runtime, "127.0.0.1", port);
    if (!connected.has_value())
        co_return;
    auto sock = std::move(*connected);

    auto const request = std::string { "GET /hello HTTP/1.1\r\nHost: localhost\r\n\r\n" };
    auto const reqBytes =
        std::span<std::byte const> { reinterpret_cast<std::byte const*>(request.data()), request.size() };
    if (!(co_await sock->write(reqBytes)).has_value())
        co_return;

    auto chunk = std::array<std::byte, 1024> {};
    while (true)
    {
        auto const got = co_await sock->read(chunk);
        if (!got.has_value() || *got == 0)
            break;
        out->append(reinterpret_cast<char const*>(chunk.data()), *got);
    }
}

/// Runs the server and client flows concurrently and captures the response.
Task<void> roundTrip(TuiRuntime* runtime, endo::net::IListener* listener, std::string* response)
{
    co_await endo::coro::whenAll(serveOne(runtime, listener),
                                 httpGet(runtime, listener->localPort(), response));
}

/// Issues one GET against @p listener through the production serve() accept-loop. The
/// client and the server are raced with `whenAny`: when the client's round-trip finishes
/// it wins, and the still-running serve() loop is cancelled (its parked accept unwinds on
/// `OperationCancelled`) — the same portable cancellation path the builtin uses on Ctrl+C,
/// rather than closing the listener from another flow (which does not wake a parked accept
/// on every platform). Mirrors the builtin's dispatch: a stateful @p handler is invoked
/// with the request path and its result becomes the body.
Task<void> serveLoopRoundTrip(TuiRuntime* runtime,
                              endo::net::IListener* listener,
                              endo::net::HttpHandler handler,
                              std::string* response)
{
    auto const port = listener->localPort();
    static_cast<void>(co_await endo::coro::whenAny(endo::net::serve(listener, std::move(handler)),
                                                   httpGet(runtime, port, response)));
}

} // namespace

TEST_CASE("HttpServer parses a request and writes a response over loopback", "[net][http]")
{
    auto source = PollEventSource {};
    auto runtime = TuiRuntime { source };

    auto listener = endo::net::listen(runtime, "127.0.0.1", 0);
    REQUIRE(listener.has_value());

    auto response = std::string {};
    runtime.blockOn(roundTrip(&runtime, listener->get(), &response));

    REQUIRE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    REQUIRE(response.find("you asked for /hello") != std::string::npos);
    REQUIRE(response.find("Content-Length: 20") != std::string::npos);
}

TEST_CASE("HttpServer serve() dispatches a request to the handler and returns its body", "[net][http]")
{
    // Exercises the full production accept-loop (serve() -> handleConnection() ->
    // readRequest/handler/writeResponse) that the httpServe builtin runs, with a handler
    // that derives the body from the request path — the same string -> string contract the
    // scripting handler uses.
    auto source = PollEventSource {};
    auto runtime = TuiRuntime { source };

    auto listener = endo::net::listen(runtime, "127.0.0.1", 0);
    REQUIRE(listener.has_value());

    auto handler = [](HttpRequest const& request) {
        return HttpResponse::ok("handled " + request.path);
    };

    auto response = std::string {};
    runtime.blockOn(serveLoopRoundTrip(&runtime, listener->get(), handler, &response));

    REQUIRE(response.find("HTTP/1.1 200 OK") != std::string::npos);
    REQUIRE(response.find("handled /hello") != std::string::npos);
    REQUIRE(response.find("Content-Length: 14") != std::string::npos);
}

TEST_CASE("HttpRequest::header looks up headers case-insensitively", "[net][http]")
{
    auto request = HttpRequest {};
    request.headers.emplace_back("Content-Type", "text/plain");
    REQUIRE(request.header("content-type") == "text/plain");
    REQUIRE(request.header("CONTENT-TYPE") == "text/plain");
    REQUIRE(request.header("missing").empty());
}
