// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <agent/mcp/JsonRpc.hpp>

using namespace endo::agent::mcp::jsonrpc;

TEST_CASE("jsonrpc.makeRequest", "[mcp][jsonrpc]")
{
    SECTION("produces valid JSON-RPC 2.0 request")
    {
        auto const req = makeRequest(1, "test/method");
        CHECK(req["jsonrpc"] == "2.0");
        CHECK(req["id"] == 1);
        CHECK(req["method"] == "test/method");
        CHECK(!req.contains("params"));
    }

    SECTION("includes params when provided")
    {
        auto params = nlohmann::json { { "key", "value" } };
        auto const req = makeRequest(42, "tools/call", std::move(params));
        CHECK(req["id"] == 42);
        CHECK(req["params"]["key"] == "value");
    }
}

TEST_CASE("jsonrpc.makeNotification", "[mcp][jsonrpc]")
{
    SECTION("produces notification without id")
    {
        auto const notif = makeNotification("notifications/initialized");
        CHECK(notif["jsonrpc"] == "2.0");
        CHECK(notif["method"] == "notifications/initialized");
        CHECK(!notif.contains("id"));
        CHECK(!notif.contains("params"));
    }

    SECTION("includes params when provided")
    {
        auto params = nlohmann::json { { "foo", 123 } };
        auto const notif = makeNotification("test/notify", std::move(params));
        CHECK(notif["params"]["foo"] == 123);
    }
}

TEST_CASE("jsonrpc.parseResponse", "[mcp][jsonrpc]")
{
    SECTION("parses success response")
    {
        auto const msg = nlohmann::json {
            { "jsonrpc", "2.0" },
            { "id", 1 },
            { "result", { { "status", "ok" } } },
        };
        auto const result = parseResponse(msg);
        REQUIRE(result.has_value());
        CHECK(result->isSuccess());
        CHECK(result->result.has_value());
        CHECK((*result->result)["status"] == "ok");
    }

    SECTION("parses error response")
    {
        auto const msg = nlohmann::json {
            { "jsonrpc", "2.0" },
            { "id", 1 },
            { "error", { { "code", -32600 }, { "message", "Invalid Request" } } },
        };
        auto const result = parseResponse(msg);
        REQUIRE(result.has_value());
        CHECK(!result->isSuccess());
        REQUIRE(result->error.has_value());
        CHECK(result->error->code == -32600);
        CHECK(result->error->message == "Invalid Request");
    }

    SECTION("rejects non-JSON-RPC 2.0 message")
    {
        auto const msg = nlohmann::json { { "jsonrpc", "1.0" } };
        auto const result = parseResponse(msg);
        CHECK(!result.has_value());
        CHECK(result.error().code == endo::agent::mcp::McpErrorCode::ProtocolError);
    }

    SECTION("rejects message without result, error, or method")
    {
        auto const msg = nlohmann::json {
            { "jsonrpc", "2.0" },
            { "id", 1 },
        };
        auto const result = parseResponse(msg);
        CHECK(!result.has_value());
    }

    SECTION("accepts notification (has method, no result/error)")
    {
        auto const msg = nlohmann::json {
            { "jsonrpc", "2.0" },
            { "method", "notifications/tools/list_changed" },
        };
        auto const result = parseResponse(msg);
        CHECK(result.has_value());
    }
}
