// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace endo::platform
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

    /// Lists filesystem entries matching the given path.
    /// @param path A directory path (lists contents), a single file path (returns one entry),
    ///             or a glob pattern like "*.md" (returns matching entries in the parent directory).
    /// @return A vector of FileEntry structs, sorted by name.
    [[nodiscard]] virtual std::vector<FileEntry> listDirectory(std::string const& path) const = 0;
};

} // namespace endo::platform

// Backward-compatible aliases in the endo namespace
namespace endo
{
using endo::platform::FileEntry;
using endo::platform::FileInfoProvider;
} // namespace endo
