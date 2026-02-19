// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <expected>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include <agent/Types.hpp>

namespace endo::agent
{

/// Writes structured JSONL trace files recording tool call I/O.
///
/// Each line in the output file is a self-contained JSON object.
/// The first line is a session header; subsequent lines are tool call entries.
class ToolTracer
{
  public:
    /// @brief Creates a ToolTracer that writes to the given path.
    /// @param path Target JSONL file path. Parent directories are created as needed.
    /// @return The tracer, or an error message if the file could not be opened.
    [[nodiscard]] static auto create(std::filesystem::path path) -> std::expected<ToolTracer, std::string>;

    /// @brief Writes the session header line with provider and model information.
    /// @param providerName The LLM provider name (e.g. "claude").
    /// @param modelName The model identifier (e.g. "claude-sonnet-4-5-20250929").
    void writeSessionHeader(std::string_view providerName, std::string_view modelName);

    /// @brief Writes a single tool call trace entry.
    /// @param entry The completed trace entry to record.
    void writeToolCall(ToolTraceEntry const& entry);

    /// @brief Returns the trace file path.
    [[nodiscard]] auto path() const noexcept -> std::filesystem::path const&;

  private:
    explicit ToolTracer(std::filesystem::path path, std::ofstream stream);

    std::filesystem::path _path;
    std::ofstream _stream;
};

} // namespace endo::agent
