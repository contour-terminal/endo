// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <agent/mcp/Transport.hpp>

namespace endo::agent::mcp
{

/// @brief Configuration for spawning an MCP server process.
struct StdioTransportConfig
{
    std::string command;
    std::vector<std::string> args;
    std::map<std::string, std::string> env;
};

/// @brief Transport that communicates with an MCP server via stdio pipes.
///
/// Spawns a child process and communicates via its stdin/stdout.
class StdioTransport: public Transport
{
  public:
    StdioTransport();
    ~StdioTransport() override;

    StdioTransport(StdioTransport const&) = delete;
    StdioTransport& operator=(StdioTransport const&) = delete;

    /// @brief Starts the MCP server process.
    /// @param config The process configuration.
    /// @return Success or an error.
    [[nodiscard]] auto start(StdioTransportConfig const& config) -> McpVoidResult;

    [[nodiscard]] auto send(nlohmann::json const& message) -> McpVoidResult override;
    [[nodiscard]] auto receive() -> McpResult<nlohmann::json> override;
    void close() override;
    [[nodiscard]] auto isConnected() const -> bool override;

  private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace endo::agent::mcp
