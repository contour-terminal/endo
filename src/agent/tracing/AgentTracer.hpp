// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <expected>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <agent/Types.hpp>

namespace endo::agent
{

/// Writes structured JSONL trace files recording the full agent I/O flow.
///
/// Each line in the output file is a self-contained JSON object.
/// The first line is a session header; subsequent lines are events:
/// user messages, LLM requests/responses, tool calls, compaction events, and errors.
class AgentTracer
{
  public:
    /// @brief Creates an AgentTracer that writes to the given path.
    /// @param path Target JSONL file path. Parent directories are created as needed.
    /// @return The tracer, or an error message if the file could not be opened.
    [[nodiscard]] static auto create(std::filesystem::path path) -> std::expected<AgentTracer, std::string>;

    /// @brief Writes the session header line with provider and model information.
    /// @param providerName The LLM provider name (e.g. "claude").
    /// @param modelName The model identifier (e.g. "claude-sonnet-4-5-20250929").
    void writeSessionHeader(std::string_view providerName, std::string_view modelName);

    /// @brief Writes a single tool call trace entry.
    /// @param entry The completed trace entry to record.
    void writeToolCall(ToolTraceEntry const& entry);

    /// @brief Writes a user message event.
    /// @param mode The session mode ("chat" or "plan").
    /// @param content The user's message text.
    void writeUserMessage(std::string_view mode, std::string_view content);

    /// @brief Writes an LLM request event (before calling the provider).
    /// @param iteration The current tool loop iteration index.
    /// @param messageCount Number of messages in the conversation history.
    /// @param tokenEstimate Estimated token count of the conversation.
    void writeLlmRequest(size_t iteration, size_t messageCount, size_t tokenEstimate);

    /// @brief Writes an LLM response event (after the provider returns).
    /// @param iteration The current tool loop iteration index.
    /// @param hasToolCalls Whether the response contains tool calls.
    /// @param toolCount Number of tool calls in the response.
    /// @param textLength Length of the text content in the response.
    /// @param duration Time taken for the provider to generate the response.
    /// @param textContent The assistant's actual text response.
    /// @param toolCalls Tool calls the model requested.
    /// @param usage Token usage statistics, if available.
    void writeLlmResponse(size_t iteration,
                          bool hasToolCalls,
                          size_t toolCount,
                          size_t textLength,
                          std::chrono::milliseconds duration,
                          std::string_view textContent,
                          std::span<ToolCall const> toolCalls,
                          std::optional<TokenUsage> const& usage,
                          std::string_view url = {},
                          std::string_view requestBody = {});

    /// @brief Writes a conversation compaction event.
    /// @param beforeMessages Number of messages before compaction.
    /// @param afterMessages Number of messages after compaction.
    /// @param beforeTokens Estimated token count before compaction.
    /// @param afterTokens Estimated token count after compaction.
    void writeCompaction(size_t beforeMessages,
                         size_t afterMessages,
                         size_t beforeTokens,
                         size_t afterTokens);

    /// @brief Writes an error event with optional HTTP I/O context.
    /// @param code A short error code (e.g. "ProviderError", "ToolLoopExceeded").
    /// @param message Descriptive error message.
    /// @param url HTTP request URL (omitted from output when empty).
    /// @param requestBody HTTP request body (omitted from output when empty).
    /// @param responseBody HTTP response body (omitted from output when empty).
    void writeError(std::string_view code,
                    std::string_view message,
                    std::string_view url = {},
                    std::string_view requestBody = {},
                    std::string_view responseBody = {});

    /// @brief Returns the trace file path.
    [[nodiscard]] auto path() const noexcept -> std::filesystem::path const&;

  private:
    explicit AgentTracer(std::filesystem::path path, std::ofstream stream);

    /// Serializes a JSON document as a single line and flushes to the output stream.
    void writeLine(nlohmann::json const& doc);

    std::filesystem::path _path;
    std::ofstream _stream;
};

/// Resolves the trace log directory based on the current working directory.
///
/// Walks from CWD upward looking for a `.git` directory.
/// If found, returns `<project-root>/.endo/trace-logs/`.
/// Otherwise, returns `~/.local/state/endo/trace-logs/`.
[[nodiscard]] auto resolveTraceLogDirectory() -> std::filesystem::path;

/// Removes the oldest trace files exceeding @p maxFiles in the given directory.
///
/// Lists `*.jsonl` files in @p dir, sorted by last-write-time, and deletes
/// the oldest entries so that at most @p maxFiles remain.
/// @param dir Directory containing trace log files.
/// @param maxFiles Maximum number of trace files to retain.
void pruneOldTraceFiles(std::filesystem::path const& dir, size_t maxFiles);

} // namespace endo::agent
