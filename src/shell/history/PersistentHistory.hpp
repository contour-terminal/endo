// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "History.hpp"

#include <platform/FileSystem.hpp>

namespace endo
{

/// @brief A single entry in the persistent command history.
struct HistoryEntry
{
    std::string command;                                ///< The command text.
    std::chrono::system_clock::time_point lastExecuted; ///< When the command was last executed.
    uint32_t executionCount = 1;                        ///< Total execution count.
    bool persisted = false;                             ///< Whether this entry should be saved to disk.
};

/// @brief Persistent command history that saves to a YAML file on disk.
///
/// Tracks execution frequency and timestamps for smarter completion ranking.
/// New commands are only persisted after successful execution (exit code 0).
/// On first run, auto-imports history from fish, zsh, or bash.
class PersistentHistory final: public History
{
  public:
    /// @brief Constructs a persistent history with the given filesystem and maximum size.
    /// @param fs The filesystem interface to use for reading/writing history files.
    /// @param maxSize Maximum number of entries to store (default: 5000).
    explicit PersistentHistory(FileSystem const& fs, size_t maxSize = 5000);

    void add(std::string entry) override;
    [[nodiscard]] std::vector<std::string> const& entries() const override;
    [[nodiscard]] size_t size() const override;
    [[nodiscard]] size_t maxSize() const override;
    void clear() override;
    void markLastResult(int exitCode) override;
    [[nodiscard]] std::vector<std::string_view> search(std::string_view prefix,
                                                       size_t maxResults = 10) const override;
    [[nodiscard]] std::vector<FuzzySearchResult> searchFuzzy(std::string_view prefix,
                                                             size_t maxResults = 10) const override;

    /// @brief Loads history entries from the file on disk.
    void load();

    /// @brief Imports history from other shells if our history file does not exist yet.
    void autoImportIfEmpty();

    /// @brief Sets a custom file path (primarily for testing).
    /// @param path The file path to use for reading/writing history.
    void setFilePath(std::filesystem::path path);

    /// @brief Returns the current file path.
    [[nodiscard]] std::filesystem::path const& filePath() const noexcept { return _filePath; }

    /// @brief Returns the rich entry data (primarily for testing).
    [[nodiscard]] std::vector<HistoryEntry> const& richEntries() const noexcept { return _richEntries; }

    /// @brief Returns the default history file path for the current platform.
    [[nodiscard]] static std::filesystem::path defaultHistoryPath();

    /// @brief Imports history from fish shell.
    /// @param path Path to the fish history file.
    /// @return Number of entries imported.
    [[nodiscard]] size_t importFish(std::filesystem::path const& path);

    /// @brief Imports history from zsh.
    /// @param path Path to the zsh history file.
    /// @return Number of entries imported.
    [[nodiscard]] size_t importZsh(std::filesystem::path const& path);

    /// @brief Imports history from bash.
    /// @param path Path to the bash history file.
    /// @return Number of entries imported.
    [[nodiscard]] size_t importBash(std::filesystem::path const& path);

  private:
    /// @brief Writes persisted entries to disk atomically.
    void flush();

    /// @brief Rebuilds the string entries cache from rich entries.
    void rebuildEntriesCache();

    /// @brief Evicts the oldest/least-used entries when at capacity.
    void evictIfNeeded();

    FileSystem const& _fs;                  ///< Filesystem interface for I/O.
    std::vector<HistoryEntry> _richEntries; ///< Full entry data with metadata.
    std::vector<std::string> _entries;      ///< String cache for entries() return.
    std::filesystem::path _filePath;        ///< Path to the history file.
    size_t _maxSize;                        ///< Maximum number of entries.
    bool _dirty = false;                    ///< Whether unsaved changes exist.
    std::optional<size_t> _lastAddedIndex;  ///< Index of the last added entry.
};

} // namespace endo
