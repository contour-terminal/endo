// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file HeadlessRunner.hpp
/// @brief Data types and JSON serialization for headless agent execution results.

#include <chrono>
#include <string>
#include <vector>

#include <agent/Types.hpp>
#include <nlohmann/json.hpp>

namespace endo::agent
{

/// A single recorded tool call during headless execution.
struct ToolCallRecord
{
    std::string name;                         ///< Tool name.
    nlohmann::json arguments;                 ///< Tool input arguments.
    std::string result;                       ///< Tool output content.
    bool isError = false;                     ///< Whether the tool returned an error.
    std::chrono::milliseconds duration { 0 }; ///< Execution duration.
};

/// Result of a complete headless agent run.
struct HeadlessRunResult
{
    bool success = false;                  ///< Whether the run completed without error.
    std::string response;                  ///< The final assistant response text.
    std::string errorMessage;              ///< Error message if !success.
    std::vector<ToolCallRecord> toolCalls; ///< All tool calls made during the run.
    TokenUsage tokenUsage;                 ///< Cumulative token usage.
    int turnCount = 0;                     ///< Number of turns completed.
    std::string providerName;              ///< Provider used (e.g., "claude").
    std::string modelName;                 ///< Model used (e.g., "claude-sonnet-4-6").
};

/// Serializes a HeadlessRunResult to JSON.
/// @param result The result to serialize.
/// @return JSON object with structured output.
[[nodiscard]] auto toJson(HeadlessRunResult const& result) -> nlohmann::json;

} // namespace endo::agent
