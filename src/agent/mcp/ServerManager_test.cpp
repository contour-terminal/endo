// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <deque>
#include <mutex>
#include <thread>

#include <agent/mcp/McpClient.hpp>
#include <agent/mcp/ServerManager.hpp>
#include <agent/mcp/Transport.hpp>

using namespace endo::agent;
using namespace endo::agent::mcp;

namespace
{

/// @brief Thread-safe mock transport for testing ServerManager.
class MockTransport final: public Transport
{
  public:
    void queueResponse(nlohmann::json response)
    {
        {
            auto lock = std::unique_lock(_mutex);
            _responses.push_back(std::move(response));
        }
        _cv.notify_one();
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

auto makeToolListResponse(int id, std::vector<nlohmann::json> tools) -> nlohmann::json
{
    return nlohmann::json {
        { "jsonrpc", "2.0" },
        { "id", id },
        { "result", { { "tools", std::move(tools) } } },
    };
}

auto makeToolCallResponse(int id, std::string const& text) -> nlohmann::json
{
    return nlohmann::json {
        { "jsonrpc", "2.0" },
        { "id", id },
        { "result",
          { { "content", nlohmann::json::array({ { { "type", "text" }, { "text", text } } }) },
            { "isError", false } } },
    };
}

auto makeToolJson(std::string const& name, std::string const& desc = "A tool") -> nlohmann::json
{
    return nlohmann::json {
        { "name", name },
        { "description", desc },
        { "inputSchema", { { "type", "object" } } },
    };
}

/// @brief Creates an initialized McpClient backed by a MockTransport.
///        Returns both the client and a raw pointer to the transport for queueing.
struct ClientAndTransport
{
    std::unique_ptr<McpClient> client;
    MockTransport* transport;
};

auto makeInitializedClient(std::vector<nlohmann::json> tools) -> ClientAndTransport
{
    auto transport = std::make_unique<MockTransport>();
    auto* tp = transport.get();

    // Queue initialize response + tools/list response (id=2 since init is id=1).
    tp->queueResponse(makeInitializeResponse());
    tp->queueResponse(makeToolListResponse(2, std::move(tools)));

    auto client = std::make_unique<McpClient>(std::move(transport));
    auto initResult = client->initialize();
    if (!initResult.has_value())
        throw std::runtime_error("Failed to initialize mock client");

    return { std::move(client), tp };
}

} // namespace

TEST_CASE("ServerManager.initial_state", "[mcp][server_manager]")
{
    auto manager = ServerManager {};
    CHECK(manager.serverCount() == 0);
    CHECK(manager.allTools().empty());
}

TEST_CASE("ServerManager.callTool_unknown", "[mcp][server_manager]")
{
    auto manager = ServerManager {};
    auto result = manager.callTool("nonexistent_tool", {});
    REQUIRE(!result.has_value());
    CHECK(result.error().code == McpErrorCode::ToolCallError);
}

TEST_CASE("ServerManager.shutdown", "[mcp][server_manager]")
{
    auto manager = ServerManager {};
    manager.shutdown();
    CHECK(manager.serverCount() == 0);
}

TEST_CASE("ServerManager.addServerWithClient", "[mcp][server_manager]")
{
    SECTION("adds server and lists tools")
    {
        auto [client, tp] = makeInitializedClient({ makeToolJson("tool_a"), makeToolJson("tool_b") });

        auto manager = ServerManager {};
        auto result = manager.addServerWithClient("test-server", std::move(client));
        REQUIRE(result.has_value());

        CHECK(manager.serverCount() == 1);
        auto const tools = manager.allTools();
        REQUIRE(tools.size() == 2);
        CHECK(tools[0].name == "tool_a");
        CHECK(tools[1].name == "tool_b");
    }

    SECTION("routes tool calls correctly")
    {
        auto [client, tp] = makeInitializedClient({ makeToolJson("my_tool") });

        // Queue a tool call response (id=3: init=1, listTools=2, callTool=3).
        tp->queueResponse(makeToolCallResponse(3, "tool output"));

        auto manager = ServerManager {};
        REQUIRE(manager.addServerWithClient("srv", std::move(client)).has_value());

        auto result = manager.callTool("my_tool", {});
        REQUIRE(result.has_value());
        CHECK(result->content == "tool output");
    }
}

TEST_CASE("ServerManager.refreshTools", "[mcp][server_manager]")
{
    SECTION("updates tool list")
    {
        auto [client, tp] = makeInitializedClient({ makeToolJson("tool_a") });

        auto manager = ServerManager {};
        REQUIRE(manager.addServerWithClient("srv", std::move(client)).has_value());
        CHECK(manager.allTools().size() == 1);

        // Queue a new tools/list response with tool_b instead of tool_a.
        tp->queueResponse(makeToolListResponse(3, { makeToolJson("tool_b") }));

        auto result = manager.refreshTools("srv");
        REQUIRE(result.has_value());

        auto const tools = manager.allTools();
        REQUIRE(tools.size() == 1);
        CHECK(tools[0].name == "tool_b");
    }

    SECTION("updates routing map")
    {
        auto [client, tp] = makeInitializedClient({ makeToolJson("old_tool") });

        auto manager = ServerManager {};
        REQUIRE(manager.addServerWithClient("srv", std::move(client)).has_value());

        // Refresh: replace old_tool with new_tool.
        tp->queueResponse(makeToolListResponse(3, { makeToolJson("new_tool") }));
        REQUIRE(manager.refreshTools("srv").has_value());

        // Old tool should no longer route.
        auto oldResult = manager.callTool("old_tool", {});
        CHECK(!oldResult.has_value());

        // New tool should route.
        tp->queueResponse(makeToolCallResponse(4, "new output"));
        auto newResult = manager.callTool("new_tool", {});
        REQUIRE(newResult.has_value());
        CHECK(newResult->content == "new output");
    }

    SECTION("invokes ToolsChangedCallback")
    {
        auto [client, tp] = makeInitializedClient({ makeToolJson("keep"), makeToolJson("remove_me") });

        auto manager = ServerManager {};
        REQUIRE(manager.addServerWithClient("srv", std::move(client)).has_value());

        auto callbackCalled = false;
        auto addedNames = std::vector<std::string> {};
        auto removedNames = std::vector<std::string> {};

        manager.setToolsChangedCallback([&](std::string_view /*serverName*/,
                                            std::span<ToolDefinition const> added,
                                            std::vector<std::string> const& removed) {
            callbackCalled = true;
            for (auto const& t: added)
                addedNames.push_back(t.name);
            removedNames = removed;
        });

        // Refresh: keep "keep", remove "remove_me", add "new_one".
        tp->queueResponse(makeToolListResponse(3, { makeToolJson("keep"), makeToolJson("new_one") }));
        REQUIRE(manager.refreshTools("srv").has_value());

        CHECK(callbackCalled);
        REQUIRE(addedNames.size() == 1);
        CHECK(addedNames[0] == "new_one");
        REQUIRE(removedNames.size() == 1);
        CHECK(removedNames[0] == "remove_me");
    }

    SECTION("returns error for nonexistent server")
    {
        auto manager = ServerManager {};
        auto result = manager.refreshTools("nonexistent");
        CHECK(!result.has_value());
    }
}

TEST_CASE("ServerManager.processNotifications", "[mcp][server_manager]")
{
    SECTION("triggers refresh on tools/list_changed")
    {
        auto [client, tp] = makeInitializedClient({ makeToolJson("initial_tool") });

        auto manager = ServerManager {};
        REQUIRE(manager.addServerWithClient("srv", std::move(client)).has_value());
        CHECK(manager.allTools().size() == 1);

        // Inject a notification into the transport.
        tp->queueResponse(nlohmann::json {
            { "jsonrpc", "2.0" },
            { "method", "notifications/tools/list_changed" },
        });

        // Give I/O thread time to buffer the notification.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Queue the refresh response that processNotifications will trigger.
        tp->queueResponse(makeToolListResponse(3, { makeToolJson("refreshed_tool") }));

        manager.processNotifications();

        auto const tools = manager.allTools();
        REQUIRE(tools.size() == 1);
        CHECK(tools[0].name == "refreshed_tool");
    }

    SECTION("no-op when no notifications")
    {
        auto [client, tp] = makeInitializedClient({ makeToolJson("tool_a") });

        auto manager = ServerManager {};
        REQUIRE(manager.addServerWithClient("srv", std::move(client)).has_value());

        // processNotifications should not crash or change anything.
        manager.processNotifications();

        CHECK(manager.allTools().size() == 1);
    }
}
