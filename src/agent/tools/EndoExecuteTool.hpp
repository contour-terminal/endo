// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <functional>
#include <string>

#include <agent/tools/AgentTool.hpp>

namespace endo::agent
{

/// Result of evaluating endo source code.
struct EndoExecResult
{
    std::string output;    ///< Captured stdout and stderr.
    int exitCode = 0;      ///< Exit code from evaluation.
    bool timedOut = false; ///< Whether the evaluation timed out.
};

/// Callback type for evaluating endo source code.
/// @param source The endo source code to evaluate.
/// @param timeout Maximum evaluation time.
/// @return The evaluation result.
using EndoExecuteCallback =
    std::function<EndoExecResult(std::string const& source, std::chrono::milliseconds timeout)>;

/// Tool for evaluating endo source code directly and returning captured output.
///
/// Input: { source: string, timeout_ms?: int }
/// - source: The endo source code to evaluate (required).
/// - timeout_ms: Maximum execution time in milliseconds (default: 120000, max: 600000).
///
/// Delegates evaluation to a callback provided at construction time.
/// Output is truncated at 30KB.
class EndoExecuteTool final: public AgentTool
{
  public:
    /// @brief Constructs an endo execution tool with the given callback.
    /// @param executeCallback Callback that performs the actual endo evaluation.
    explicit EndoExecuteTool(EndoExecuteCallback executeCallback);

    [[nodiscard]] auto name() const noexcept -> std::string_view override;
    [[nodiscard]] auto definition() const -> ToolDefinition override;
    [[nodiscard]] auto execute(nlohmann::json const& arguments)
        -> std::expected<ToolResult, ToolError> override;

  private:
    EndoExecuteCallback _executeCallback;
};

} // namespace endo::agent
