// SPDX-License-Identifier: Apache-2.0
#include <tui/runtime/PollEventSource.hpp>
#include <tui/runtime/TuiRuntime.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include <string_view>

#include <coro/Task.hpp>
#include <coro/WhenAll.hpp>
#include <net/IListener.hpp>
#include <net/ISocket.hpp>
#include <net/Sockets.hpp>
#include <net/testing/InMemoryTransport.hpp>

using endo::coro::Task;
using endo::net::ISocket;
using tui::runtime::PollEventSource;
using tui::runtime::TuiRuntime;

namespace
{

/// Reads exactly @p expected.size() bytes from @p sock and checks they match.
Task<void> expectRead(ISocket* sock, std::string_view expected, bool* ok)
{
    auto buffer = std::array<std::byte, 64> {};
    std::size_t total = 0;
    while (total < expected.size())
    {
        auto const result = co_await sock->read(std::span<std::byte> { buffer }.subspan(total));
        if (!result.has_value() || *result == 0)
            break;
        total += *result;
    }
    *ok = total == expected.size() && std::memcmp(buffer.data(), expected.data(), expected.size()) == 0;
}

/// Writes @p data to @p sock.
Task<void> writeAll(ISocket* sock, std::string_view data, bool* ok)
{
    auto const bytes =
        std::span<std::byte const> { reinterpret_cast<std::byte const*>(data.data()), data.size() };
    auto const result = co_await sock->write(bytes);
    *ok = result.has_value() && *result == data.size();
}

/// Drives an InMemoryTransport round-trip: write on one end, read on the other.
Task<void> pairRoundTrip(TuiRuntime* runtime, bool* wroteOk, bool* readOk)
{
    auto pair = endo::net::testing::makeSocketPair(*runtime);
    REQUIRE(pair.has_value());
    auto first = std::move(pair->first);
    auto second = std::move(pair->second);

    co_await endo::coro::whenAll(writeAll(first.get(), "ping", wroteOk),
                                 expectRead(second.get(), "ping", readOk));
}

/// The server flow: accept one connection, read a request, echo it back.
Task<void> echoServer(TuiRuntime* runtime, endo::net::IListener* listener, bool* served)
{
    auto accepted = co_await listener->accept();
    if (!accepted.has_value())
        co_return;
    auto conn = std::move(*accepted);

    auto buffer = std::array<std::byte, 64> {};
    auto const got = co_await conn->read(buffer);
    if (!got.has_value() || *got == 0)
        co_return;
    auto const echoed = co_await conn->write(std::span<std::byte const> { buffer }.subspan(0, *got));
    *served = echoed.has_value() && *echoed == *got;
}

/// The client flow: connect, send a request, read the echo, compare.
Task<void> echoClient(TuiRuntime* runtime, std::uint16_t port, bool* matched)
{
    auto connected = co_await endo::net::connect(runtime, "127.0.0.1", port);
    if (!connected.has_value())
        co_return;
    auto sock = std::move(*connected);

    bool wroteOk = false;
    co_await writeAll(sock.get(), "hello", &wroteOk);
    if (!wroteOk)
        co_return;
    co_await expectRead(sock.get(), "hello", matched);
}

/// Runs the loopback echo: server and client flows concurrently on one runtime.
Task<void> loopbackEcho(TuiRuntime* runtime, endo::net::IListener* listener, bool* served, bool* matched)
{
    co_await endo::coro::whenAll(echoServer(runtime, listener, served),
                                 echoClient(runtime, listener->localPort(), matched));
}

} // namespace

TEST_CASE("InMemoryTransport round-trips bytes between connected endpoints", "[net]")
{
    auto source = PollEventSource {};
    auto runtime = TuiRuntime { source };

    auto wroteOk = false;
    auto readOk = false;
    runtime.blockOn(pairRoundTrip(&runtime, &wroteOk, &readOk));

    REQUIRE(wroteOk);
    REQUIRE(readOk);
}

TEST_CASE("listen + connect + accept echo a request over loopback", "[net][poll]")
{
    auto source = PollEventSource {};
    auto runtime = TuiRuntime { source };

    auto listener = endo::net::listen(runtime, "127.0.0.1", 0);
    REQUIRE(listener.has_value());
    REQUIRE((*listener)->localPort() != 0);

    auto served = false;
    auto matched = false;
    runtime.blockOn(loopbackEcho(&runtime, listener->get(), &served, &matched));

    REQUIRE(served);
    REQUIRE(matched);
}
