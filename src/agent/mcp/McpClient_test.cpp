// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <deque>
#include <mutex>
#include <thread>

#include <agent/mcp/McpClient.hpp>
#include <agent/mcp/Transport.hpp>

using namespace endo::agent;
using namespace endo::agent::mcp;

namespace
{

/// @brief Mock transport that returns canned responses for testing.
///
/// Thread-safe: receive() blocks when no responses are available and unblocks
/// when a response is queued or when close() is called. send() is also safe
/// for concurrent access.
class MockTransport final: public Transport
{
  public:
    /// @brief Queues a JSON response to be returned by receive().
    void queueResponse(nlohmann::json response)
    {
        {
            auto lock = std::unique_lock(_mutex);
            _responses.push_back(std::move(response));
        }
        _cv.notify_one();
    }

    /// @brief Returns the list of messages sent via send().
    [[nodiscard]] auto sentMessages() -> std::deque<nlohmann::json>
    {
        auto lock = std::unique_lock(_mutex);
        return _sent;
    }

    [[nodiscard]] auto send(nlohmann::json const& message) -> McpVoidResult override
    {
        auto lock = std::unique_lock(_mutex);
        _sent.push_back(message);
        return {};
    }

    [[nodiscard]] auto receive() -> McpResult<nlohmann::json> override
    {
        auto lock = std::unique_lock(_mutex);
        _cv.wait(lock, [this] { return !_responses.empty() || !_connected; });

        if (!_connected && _responses.empty())
            return makeMcpError(McpErrorCode::TransportError, "Transport closed");

        auto msg = std::move(_responses.front());
        _responses.pop_front();
        return msg;
    }

    void close() override
    {
        {
            auto lock = std::unique_lock(_mutex);
            _connected = false;
        }
        _cv.notify_all();
    }

    [[nodiscard]] auto isConnected() const -> bool override
    {
        auto lock = std::unique_lock(_mutex);
        return _connected;
    }

  private:
    mutable std::mutex _mutex;
    std::condition_variable _cv;
    std::deque<nlohmann::json> _responses;
    std::deque<nlohmann::json> _sent;
    bool _connected = true;
};

/// @brief Creates an initialize response for the mock transport.
auto makeInitializeResponse() -> nlohmann::json
{
    return nlohmann::json {
        { "jsonrpc", "2.0" },
        { "id", 1 },
        { "result",
          { { "protocolVersion", "2024-11-05" },
            { "serverInfo", { { "name", "test-server" }, { "version", "1.0.0" } } },
            { "capabilities", { { "tools", nlohmann::json::object() } } } } },
    };
}

/// @brief Creates a tools/list response with the given tools.
auto makeToolListResponse(int id, std::vector<nlohmann::json> tools) -> nlohmann::json
{
    return nlohmann::json {
        { "jsonrpc", "2.0" },
        { "id", id },
        { "result", { { "tools", std::move(tools) } } },
    };
}

/// @brief Creates a tools/call response.
auto makeToolCallResponse(int id, std::string const& text, bool isError = false) -> nlohmann::json
{
    return nlohmann::json {
        { "jsonrpc", "2.0" },
        { "id", id },
        { "result",
          { { "content", nlohmann::json::array({ { { "type", "text" }, { "text", text } } }) },
            { "isError", isError } } },
    };
}

/// @brief Creates a JSON-RPC notification message.
auto makeNotification(std::string const& method) -> nlohmann::json
{
    return nlohmann::json {
        { "jsonrpc", "2.0" },
        { "method", method },
    };
}

} // namespace

TEST_CASE("McpClient.initialize", "[mcp][client]")
{
    SECTION("successful handshake")
    {
        auto transport = std::make_unique<MockTransport>();
        auto* transportPtr = transport.get();
        transport->queueResponse(makeInitializeResponse());

        auto client = McpClient(std::move(transport));
        auto result = client.initialize();

        REQUIRE(result.has_value());
        CHECK(result->serverName == "test-server");
        CHECK(result->serverVersion == "1.0.0");
        CHECK(result->hasTools);
        CHECK(!result->hasResources);
        CHECK(client.isInitialized());

        // Verify that both the initialize request and the initialized notification were sent
        auto const sent = transportPtr->sentMessages();
        REQUIRE(sent.size() == 2);
        CHECK(sent[0]["method"] == "initialize");
        CHECK(sent[1]["method"] == "notifications/initialized");
    }

    SECTION("transport error during initialize")
    {
        auto transport = std::make_unique<MockTransport>();
        // Queue an error response
        transport->queueResponse(nlohmann::json {
            { "jsonrpc", "2.0" },
            { "id", 1 },
            { "error", { { "code", -32600 }, { "message", "Server error" } } },
        });

        auto client = McpClient(std::move(transport));
        auto result = client.initialize();

        CHECK(!result.has_value());
        CHECK(!client.isInitialized());
    }
}

TEST_CASE("McpClient.listTools", "[mcp][client]")
{
    SECTION("returns tool definitions")
    {
        auto transport = std::make_unique<MockTransport>();
        transport->queueResponse(makeInitializeResponse());
        transport->queueResponse(
            makeToolListResponse(2,
                                 { { { "name", "read_file" },
                                     { "description", "Read a file" },
                                     { "inputSchema",
                                       { { "type", "object" },
                                         { "properties", { { "path", { { "type", "string" } } } } } } } } }));

        auto client = McpClient(std::move(transport));
        REQUIRE(client.initialize().has_value());

        auto result = client.listTools();
        REQUIRE(result.has_value());
        REQUIRE(result->size() == 1);
        CHECK((*result)[0].name == "read_file");
        CHECK((*result)[0].description == "Read a file");
    }

    SECTION("fails when not initialized")
    {
        auto transport = std::make_unique<MockTransport>();
        // Need to keep the transport alive (I/O thread blocks on receive).
        auto client = McpClient(std::move(transport));
        auto result = client.listTools();
        CHECK(!result.has_value());
        CHECK(result.error().code == McpErrorCode::ProtocolError);
    }
}

TEST_CASE("McpClient.callTool", "[mcp][client]")
{
    SECTION("successful tool call")
    {
        auto transport = std::make_unique<MockTransport>();
        transport->queueResponse(makeInitializeResponse());
        transport->queueResponse(makeToolCallResponse(2, "file contents here"));

        auto client = McpClient(std::move(transport));
        REQUIRE(client.initialize().has_value());

        auto result = client.callTool("read_file", { { "path", "/tmp/test.txt" } });
        REQUIRE(result.has_value());
        CHECK(result->content == "file contents here");
        CHECK(!result->isError);
    }

    SECTION("tool call with error result")
    {
        auto transport = std::make_unique<MockTransport>();
        transport->queueResponse(makeInitializeResponse());
        transport->queueResponse(makeToolCallResponse(2, "File not found", true));

        auto client = McpClient(std::move(transport));
        REQUIRE(client.initialize().has_value());

        auto result = client.callTool("read_file", { { "path", "/nonexistent" } });
        REQUIRE(result.has_value());
        CHECK(result->content == "File not found");
        CHECK(result->isError);
    }

    SECTION("fails when not initialized")
    {
        auto transport = std::make_unique<MockTransport>();
        auto client = McpClient(std::move(transport));
        auto result = client.callTool("read_file", {});
        CHECK(!result.has_value());
    }
}

TEST_CASE("McpClient.notification_buffering", "[mcp][client]")
{
    SECTION("notification buffered by I/O thread")
    {
        auto transport = std::make_unique<MockTransport>();
        transport->queueResponse(makeInitializeResponse());

        auto client = McpClient(std::move(transport));
        auto* transportPtr = static_cast<MockTransport*>(nullptr);

        // We need the raw pointer before moving.
        // Re-do: create transport, get pointer, then move.
        auto transport2 = std::make_unique<MockTransport>();
        auto* tp = transport2.get();
        tp->queueResponse(makeInitializeResponse());

        auto client2 = McpClient(std::move(transport2));
        REQUIRE(client2.initialize().has_value());

        // Queue a notification followed by a response.
        tp->queueResponse(makeNotification("notifications/tools/list_changed"));
        tp->queueResponse(makeToolListResponse(2, {}));

        auto result = client2.listTools();
        REQUIRE(result.has_value());

        // Give I/O thread time to process notification.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        auto notifications = client2.drainNotifications();
        REQUIRE(notifications.size() == 1);
        CHECK(notifications[0].method == "notifications/tools/list_changed");
    }

    SECTION("multiple notifications buffered")
    {
        auto transport = std::make_unique<MockTransport>();
        auto* tp = transport.get();
        tp->queueResponse(makeInitializeResponse());

        auto client = McpClient(std::move(transport));
        REQUIRE(client.initialize().has_value());

        // Queue two notifications followed by a response.
        tp->queueResponse(makeNotification("notifications/tools/list_changed"));
        tp->queueResponse(makeNotification("notifications/resources/updated"));
        tp->queueResponse(makeToolListResponse(2, {}));

        auto result = client.listTools();
        REQUIRE(result.has_value());

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        auto notifications = client.drainNotifications();
        REQUIRE(notifications.size() == 2);
        CHECK(notifications[0].method == "notifications/tools/list_changed");
        CHECK(notifications[1].method == "notifications/resources/updated");
    }

    SECTION("drainNotifications clears buffer")
    {
        auto transport = std::make_unique<MockTransport>();
        auto* tp = transport.get();
        tp->queueResponse(makeInitializeResponse());

        auto client = McpClient(std::move(transport));
        REQUIRE(client.initialize().has_value());

        tp->queueResponse(makeNotification("notifications/tools/list_changed"));
        tp->queueResponse(makeToolListResponse(2, {}));

        auto result = client.listTools();
        REQUIRE(result.has_value());
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        auto first = client.drainNotifications();
        REQUIRE(first.size() == 1);

        auto second = client.drainNotifications();
        CHECK(second.empty());
    }

    SECTION("no notifications returns empty")
    {
        auto transport = std::make_unique<MockTransport>();
        auto* tp = transport.get();
        tp->queueResponse(makeInitializeResponse());

        auto client = McpClient(std::move(transport));
        REQUIRE(client.initialize().has_value());

        auto notifications = client.drainNotifications();
        CHECK(notifications.empty());
    }

    SECTION("I/O thread handles transport close")
    {
        auto transport = std::make_unique<MockTransport>();
        auto* tp = transport.get();
        tp->queueResponse(makeInitializeResponse());

        auto client = McpClient(std::move(transport));
        REQUIRE(client.initialize().has_value());

        // Close the transport — the I/O thread should push an error.
        tp->close();

        // Give the I/O thread time to detect the closure.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // The client should still be destructible without hanging.
    }
}
