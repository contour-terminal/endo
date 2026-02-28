// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <agent/Types.hpp>

namespace endo::agent::local
{

/// Result of parsing tool calls from model output.
struct ToolCallParseResult
{
    std::vector<ToolCall> toolCalls; ///< Extracted tool calls.
    std::string textContent;         ///< Non-tool-call text content.
    bool hadParsingErrors = false;   ///< Whether any JSON parsing errors occurred.
};

/// Parses tool calls from raw model output text.
///
/// Extraction priority:
/// 1. `<tool_call>...</tool_call>` XML tags
/// 2. ` ```json ... ``` ` code blocks
/// 3. Inline JSON objects with "name" and "arguments" fields
/// 4. Plain text fallback (no tool calls extracted)
///
/// @param output Raw model output text.
/// @param tools Available tool definitions (for validation).
/// @return Parsed result with tool calls and remaining text.
[[nodiscard]] auto parseToolCalls(std::string_view output, std::span<ToolDefinition const> tools)
    -> ToolCallParseResult;

} // namespace endo::agent::local
