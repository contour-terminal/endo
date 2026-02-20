// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file AgentManager.hpp
/// @brief Stub for future multi-agent coordination.
///
/// This file defines the planned interface for managing multiple concurrent agent workers.
/// It is not implemented in this phase — only the single-agent AgentWorker is functional.
///
/// ## Planned Design (Phase 4)
///
/// AgentManager will coordinate multiple AgentWorker instances for multi-agent scenarios:
///
/// - Single outbound queue tagged with `RoutedMessage { agentId, FromAgentMessage }`
///   so the main thread can demultiplex messages from multiple agents.
/// - Per-agent forwarder threads relay from per-agent rawOutbound to the shared outbound.
/// - API: `createAgent()`, `sendToAgent()`, `broadcast()`, `stopAll()`.
/// - Team leader pattern via DelegateTaskTool (tool call → inter-agent TaskDelegation).
///
/// ## Usage (Future)
///
/// ```cpp
/// auto manager = AgentManager(sharedOutbound);
/// auto id1 = manager.createAgent(config, toolRegistry);
/// auto id2 = manager.createAgent(config, toolRegistry);
/// manager.sendToAgent(id1, UserPromptMessage { "Build the frontend" });
/// manager.sendToAgent(id2, UserPromptMessage { "Build the backend" });
/// // Main thread drains sharedOutbound, routing by agentId.
/// ```

#include <cstdint>

namespace endo::agent
{

/// Identifier for an agent worker instance in a multi-agent setup.
using AgentId = uint64_t;

} // namespace endo::agent
