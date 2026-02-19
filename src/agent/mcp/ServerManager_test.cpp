// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <deque>

#include <agent/mcp/McpClient.hpp>
#include <agent/mcp/ServerManager.hpp>
#include <agent/mcp/Transport.hpp>

using namespace endo::agent;
using namespace endo::agent::mcp;

namespace
{

/// @brief Mock transport for testing ServerManager without real MCP servers.
class MockTransport final: public Transport
{
  public:
    void queueResponse(nlohmann::json response) { _responses.push_back(std::move(response)); }

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

} // namespace

// NOTE: ServerManager internally creates StdioTransport instances which spawn real processes.
// Unit tests for ServerManager focus on the allTools/callTool routing logic rather than
// end-to-end server spawning. The end-to-end flow is tested via integration tests.

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
