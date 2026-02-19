// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <deque>

#include <agent/mcp/McpClient.hpp>
#include <agent/mcp/Transport.hpp>

using namespace endo::agent;
using namespace endo::agent::mcp;

namespace
{

/// @brief Mock transport that returns canned responses for testing.
class MockTransport final: public Transport
{
  public:
    /// @brief Queues a JSON response to be returned by receive().
    void queueResponse(nlohmann::json response) { _responses.push_back(std::move(response)); }

    /// @brief Returns the list of messages sent via send().
    [[nodiscard]] auto sentMessages() const -> std::deque<nlohmann::json> const& { return _sent; }

    [[nodiscard]] auto send(nlohmann::json const& message) -> McpVoidResult override
    {
        _sent.push_back(message);
        return {};
    }

    [[nodiscard]] auto receive() -> McpResult<nlohmann::json> override
    {
        if (_responses.empty())
            return makeMcpError(McpErrorCode::TransportError, "No more responses");
        auto msg = std::move(_responses.front());
        _responses.pop_front();
        return msg;
    }

    void close() override { _connected = false; }

    [[nodiscard]] auto isConnected() const -> bool override { return _connected; }

  private:
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

} // namespace

TEST_CASE("McpClient.initialize", "[mcp][client]")
{
    SECTION("successful handshake")
    {
        auto transport = std::make_unique<MockTransport>();
        transport->queueResponse(makeInitializeResponse());
        auto* transportPtr = transport.get();

        auto client = McpClient(std::move(transport));
        auto result = client.initialize();

        REQUIRE(result.has_value());
        CHECK(result->serverName == "test-server");
        CHECK(result->serverVersion == "1.0.0");
        CHECK(result->hasTools);
        CHECK(!result->hasResources);
        CHECK(client.isInitialized());

        // Verify that both the initialize request and the initialized notification were sent
        REQUIRE(transportPtr->sentMessages().size() == 2);
        CHECK(transportPtr->sentMessages()[0]["method"] == "initialize");
        CHECK(transportPtr->sentMessages()[1]["method"] == "notifications/initialized");
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
