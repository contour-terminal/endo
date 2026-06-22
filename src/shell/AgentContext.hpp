// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// `AgentContextResult` — the result of building agent context (project files,
/// git info, system prompt) on a background thread. Shared between Shell (which
/// produces it via `buildAgentContext`) and AgentModeSession (which consumes the
/// pending future during the system-prompt handshake).

#include <string>

#include <agent/context/ProjectContextLoader.hpp>

namespace endo
{

/// @brief Result of background agent context loading.
struct AgentContextResult
{
    std::string systemPrompt;             ///< Fully built system prompt.
    std::string exploreSystemPrompt;      ///< System prompt for the explore sub-agent.
    std::string gitBranch;                ///< Current git branch name (for header display).
    std::string projectPath;              ///< Tilde-contracted project path (for header display).
    agent::ProjectContext projectContext; ///< Project context (returned for caching).
};

} // namespace endo
