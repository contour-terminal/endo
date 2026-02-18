// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
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

/// @brief Persists agent conversation history to a JSON file.
///
/// Provides atomic save (write to .tmp then rename), load, and remove operations.
/// System-role messages are excluded from persistence since the system prompt is
/// rebuilt each session. The file format includes a version field for future migration.
///
/// File format: `{"version":1,"messages":[...]}`
class ConversationHistoryStore
{
  public:
    /// @brief Constructs a store backed by the given file path.
    /// @param path Path to the JSON history file (e.g. ".endo/agent-history.json").
    explicit ConversationHistoryStore(std::filesystem::path path);

    /// @brief Loads conversation history from disk.
    /// @return The persisted messages (excluding system prompts), or empty vector if file absent.
    [[nodiscard]] auto load() const -> std::expected<std::vector<ChatMessage>, HistoryStoreError>;

    /// @brief Saves conversation history to disk atomically.
    ///
    /// Filters out Role::System messages before writing. Creates parent directories
    /// if needed. Writes to a .tmp file first, then renames for atomicity.
    /// @param messages The full conversation history to persist.
    [[nodiscard]] auto save(std::span<ChatMessage const> messages) const
        -> std::expected<void, HistoryStoreError>;

    /// @brief Removes the history file from disk.
    /// @return Success even if the file was already absent.
    [[nodiscard]] auto remove() const -> std::expected<void, HistoryStoreError>;

    /// @brief Returns the configured file path.
    [[nodiscard]] auto path() const noexcept -> std::filesystem::path const& { return _path; }

  private:
    std::filesystem::path _path;
};

} // namespace endo::agent
