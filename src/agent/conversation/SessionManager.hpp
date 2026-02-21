// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <agent/Types.hpp>
#include <agent/conversation/ConversationHistoryStore.hpp>

namespace endo::agent
{

/// @brief Manages named conversation sessions stored under a project directory.
///
/// Sessions are persisted as individual JSON files under `<projectDir>/sessions/`.
/// Each session uses ConversationHistoryStore's version 2 format with full metadata.
/// A `.last` marker file tracks the most recently active session for auto-resume.
class SessionManager
{
  public:
    /// @brief Constructs a session manager for the given project directory.
    /// @param projectDir Path to the project directory (e.g. ".endo").
    explicit SessionManager(std::filesystem::path projectDir);

    /// @brief Lists all saved sessions, sorted by updatedAt descending (most recent first).
    /// @return A vector of session metadata, or an error if the directory can't be scanned.
    [[nodiscard]] auto listSessions() const -> std::expected<std::vector<SessionMetadata>, HistoryStoreError>;

    /// @brief Saves a session with the given name.
    /// @param name Session name (used as the file stem).
    /// @param messages The conversation messages to persist.
    /// @param metadata Session metadata to include.
    [[nodiscard]] auto saveSession(std::string_view name,
                                   std::span<ChatMessage const> messages,
                                   SessionMetadata const& metadata) const
        -> std::expected<void, HistoryStoreError>;

    /// @brief Loads a session by name.
    /// @param name Session name (file stem).
    /// @return A pair of metadata and messages, or an error.
    [[nodiscard]] auto loadSession(std::string_view name) const
        -> std::expected<std::pair<SessionMetadata, std::vector<ChatMessage>>, HistoryStoreError>;

    /// @brief Removes a saved session by name.
    /// @param name Session name to delete.
    [[nodiscard]] auto removeSession(std::string_view name) const -> std::expected<void, HistoryStoreError>;

    /// @brief Returns the name of the last active session, or empty if none.
    [[nodiscard]] auto lastActiveSession() const -> std::string;

    /// @brief Records the name of the last active session for auto-resume.
    /// @param name The session name to record.
    void setLastActiveSession(std::string_view name) const;

    /// @brief Clears the last active session marker.
    void clearLastActiveSession() const;

    /// @brief Checks whether a session with the given name exists on disk.
    /// @param name Session name to check.
    [[nodiscard]] auto sessionExists(std::string_view name) const -> bool;

    /// @brief Generates a filesystem-safe session name from arbitrary text.
    ///
    /// Slugifies the first ~30 characters: lowercase, non-alphanumeric characters
    /// replaced with hyphens, consecutive hyphens collapsed, leading/trailing hyphens
    /// trimmed. Appends `-2`, `-3` etc. if a session with the generated name already exists.
    /// @param text Source text (typically the first user message).
    /// @return A unique, filesystem-safe session name.
    [[nodiscard]] auto generateSessionName(std::string_view text) const -> std::string;

    /// @brief Returns the names of all saved sessions (for tab completion).
    /// @return A sorted vector of session name strings.
    [[nodiscard]] auto sessionNames() const -> std::vector<std::string>;

    /// @brief Returns the sessions directory path.
    [[nodiscard]] auto sessionsDir() const noexcept -> std::filesystem::path const& { return _sessionsDir; }

  private:
    /// @brief Returns the file path for a session with the given name.
    [[nodiscard]] auto sessionPath(std::string_view name) const -> std::filesystem::path;

    std::filesystem::path _projectDir;
    std::filesystem::path _sessionsDir;
    std::filesystem::path _lastMarkerPath;
};

} // namespace endo::agent
