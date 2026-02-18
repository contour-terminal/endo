// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

#include <agent/AgentConfig.hpp>
#include <agent/tools/AgentTool.hpp>
#include <agent/tools/ShellExecuteTool.hpp>

namespace endo::agent
{

class LlmProvider;

/// Tool that spawns an isolated sub-agent to explore the codebase.
///
/// The inner agent has access only to read-only tools (read_file, glob, grep, git).
/// Its conversation history is discarded after execution — only the final answer
/// is returned to the outer agent's context window.
///
/// Input: { question: string (required), scope?: string }
/// - question: The exploration question to answer.
/// - scope: Optional context to narrow the exploration (e.g. "src/agent/ directory").
///
/// Output: A concise answer summarizing the exploration findings.
class ExploreTool final: public AgentTool
{
  public:
    /// @brief Constructs an explore tool with the given dependencies.
    /// @param provider The LLM provider for the inner agent session.
    /// @param shellExecCb Callback for shell execution (used by inner GitTool).
    /// @param config Configuration for the explore sub-agent.
    ExploreTool(LlmProvider& provider, ShellExecuteCallback shellExecCb, ExploreConfig config);

    /// @brief Sets or replaces the system prompt for the inner exploration agent.
    /// @param systemPrompt The system prompt text.
    void setSystemPrompt(std::string systemPrompt);

    [[nodiscard]] auto name() const noexcept -> std::string_view override;
    [[nodiscard]] auto definition() const -> ToolDefinition override;
    [[nodiscard]] auto execute(nlohmann::json const& arguments)
        -> std::expected<ToolResult, ToolError> override;

  private:
    LlmProvider& _provider;
    ShellExecuteCallback _shellExecCb;
    std::string _systemPrompt;
    ExploreConfig _config;
};

} // namespace endo::agent
