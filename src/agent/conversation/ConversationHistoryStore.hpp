// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <agent/Types.hpp>

namespace endo::agent
{

/// Error codes for conversation history store operations.
enum class HistoryStoreErrorCode : uint8_t
{
    IoError,        ///< File I/O failed (read, write, or delete).
    CorruptJson,    ///< File contains malformed JSON.
    MissingVersion, ///< JSON is valid but lacks the required "version" field.
};

/// Error information from a history store operation.
struct HistoryStoreError
{
    HistoryStoreErrorCode code;
    std::string message;
};

/// @brief Metadata about a saved conversation session.
struct SessionMetadata
{
    std::string name;                                ///< Human-readable session name.
    std::chrono::system_clock::time_point createdAt; ///< When the session was first created.
    std::chrono::system_clock::time_point updatedAt; ///< When the session was last saved.
    std::string provider;                            ///< LLM provider used (e.g. "claude").
    std::string model;                               ///< Model identifier (e.g. "claude-sonnet-4-6").
    int turnCount = 0;                               ///< Number of conversation turns.
    TokenUsage tokenUsage;                           ///< Cumulative token usage statistics.
};

/// @brief Persists agent conversation history to a JSON file.
///
/// Provides atomic save (write to .tmp then rename), load, and remove operations.
/// System-role messages are excluded from persistence since the system prompt is
/// rebuilt each session. The file format includes a version field for future migration.
///
/// Supports two file format versions:
/// - Version 1: `{"version":1,"messages":[...]}`
/// - Version 2: `{"version":2,"name":"...","created_at":"...","updated_at":"...","provider":"...",
///                "model":"...","turn_count":N,"token_usage":{...},"messages":[...]}`
class ConversationHistoryStore
{
  public:
    /// @brief Constructs a store backed by the given file path.
    /// @param path Path to the JSON history file (e.g. ".endo/agent-history.json").
    explicit ConversationHistoryStore(std::filesystem::path path);

    /// @brief Loads conversation history from disk (version 1 compatibility).
    /// @return The persisted messages (excluding system prompts), or empty vector if file absent.
    [[nodiscard]] auto load() const -> std::expected<std::vector<ChatMessage>, HistoryStoreError>;

    /// @brief Saves conversation history to disk atomically (version 1 format).
    ///
    /// Filters out Role::System messages before writing. Creates parent directories
    /// if needed. Writes to a .tmp file first, then renames for atomicity.
    /// @param messages The full conversation history to persist.
    [[nodiscard]] auto save(std::span<ChatMessage const> messages) const
        -> std::expected<void, HistoryStoreError>;

    /// @brief Saves conversation history with metadata to disk atomically (version 2 format).
    ///
    /// Writes both session metadata and messages. Filters out Role::System messages.
    /// @param messages The full conversation history to persist.
    /// @param metadata Session metadata to include in the file.
    [[nodiscard]] auto save(std::span<ChatMessage const> messages, SessionMetadata const& metadata) const
        -> std::expected<void, HistoryStoreError>;

    /// @brief Loads both session metadata and conversation history from disk.
    ///
    /// For version 1 files, returns default metadata with timestamps derived from
    /// the file's last write time.
    /// @return A pair of metadata and messages, or an error.
    [[nodiscard]] auto loadWithMetadata() const
        -> std::expected<std::pair<SessionMetadata, std::vector<ChatMessage>>, HistoryStoreError>;

    /// @brief Loads only the session metadata without deserializing messages.
    ///
    /// More efficient than loadWithMetadata() when only metadata is needed (e.g. listing sessions).
    /// For version 1 files, returns default metadata with timestamps from file modification time.
    /// @return The session metadata, or an error.
    [[nodiscard]] auto loadMetadataOnly() const -> std::expected<SessionMetadata, HistoryStoreError>;

    /// @brief Removes the history file from disk.
    /// @return Success even if the file was already absent.
    [[nodiscard]] auto remove() const -> std::expected<void, HistoryStoreError>;

    /// @brief Returns the configured file path.
    [[nodiscard]] auto path() const noexcept -> std::filesystem::path const& { return _path; }

  private:
    std::filesystem::path _path;
};

} // namespace endo::agent
