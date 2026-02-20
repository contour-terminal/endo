// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string_view>

namespace endo::agent
{

/// @brief Replays a tool trace JSONL file, printing a summary of each entry.
///
/// Reads the trace file line by line, parsing session headers and tool call entries.
/// For each tool call, prints the tool name, arguments summary, result status, and duration.
/// @param traceFilePath Path to the JSONL trace file.
/// @return Exit code (0 on success, non-zero on error).
[[nodiscard]] auto runTraceReplay(std::string_view traceFilePath) -> int;

} // namespace endo::agent
