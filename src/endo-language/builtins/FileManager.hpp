// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file FileManager.hpp
/// @brief File handle management for the Endo File I/O module.
///
/// Manages a mapping from integer handle IDs to open file streams.
/// Used by File.open/File.close/File.readLine native callbacks.

#include <cstdint>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace endo
{

/// Manages open file handles for the File I/O module.
///
/// Each open file gets a unique integer handle ID. Handles are used as
/// the payload of FileHandle typed objects in the VM.
class FileManager
{
  public:
    FileManager() = default;
    ~FileManager() = default;

    FileManager(FileManager const&) = delete;
    FileManager& operator=(FileManager const&) = delete;

    /// Opens a file with the given mode ("r", "w", "a", "rw").
    /// @return Handle ID on success, or std::nullopt on failure.
    [[nodiscard]] std::optional<int64_t> open(std::string const& path, std::string const& mode);

    /// Closes a file handle.
    /// @return true if handle was valid and closed, false if handle not found.
    bool close(int64_t handle);

    /// Reads one line from the file (without trailing newline).
    /// @return The line, or std::nullopt on EOF or invalid handle.
    [[nodiscard]] std::optional<std::string> readLine(int64_t handle);

    /// Checks if a handle is valid and the stream is open.
    [[nodiscard]] bool isOpen(int64_t handle) const;

  private:
    int64_t _nextHandle = 1;
    std::unordered_map<int64_t, std::unique_ptr<std::fstream>> _handles;
};

} // namespace endo
