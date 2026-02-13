// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace endo
{

/// Platform-independent file entry returned by FileInfoProvider.
struct FileEntry
{
    std::string name; ///< File name (not full path)
    int64_t size;     ///< File size in bytes
    int64_t mode;     ///< Permission bits (e.g. 0755)
    int64_t mtime;    ///< Last modification time as epoch seconds
    bool isDir;       ///< Whether this entry is a directory
};

/// Abstract interface for listing directory contents.
///
/// Implementations provide platform-specific directory enumeration.
/// Inject via constructor for testability (mock in tests, real in shell).
class FileInfoProvider
{
  public:
    virtual ~FileInfoProvider() = default;

    /// Enumerates all entries in the given directory.
    /// @param path The directory path to list.
    /// @return A vector of FileEntry structs, one per entry.
    [[nodiscard]] virtual std::vector<FileEntry> listDirectory(std::string const& path) const = 0;
};

} // namespace endo
