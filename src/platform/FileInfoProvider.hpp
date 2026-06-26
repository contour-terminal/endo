// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace endo::platform
{

/// Platform-independent file entry returned by FileInfoProvider.
///
/// Fields beyond @c name carry standard `stat(2)` metadata. The block/identity
/// fields (@c blocks, @c dev, @c ino) enable true disk-usage accounting, hardlink
/// deduplication, and cross-filesystem detection; platforms that cannot supply
/// them leave the documented sentinel defaults (@c blocks = -1, @c dev/@c ino = 0).
struct FileEntry
{
    std::string name;       ///< File name (not full path).
    int64_t size = 0;       ///< Apparent size in bytes (st_size).
    int64_t blocks = -1;    ///< Allocated 512-byte blocks (st_blocks); -1 if unknown.
    int64_t mode = 0;       ///< Permission bits (e.g. 0755).
    int64_t mtime = 0;      ///< Last modification time as epoch seconds.
    uint64_t dev = 0;       ///< Device id (st_dev) for cross-filesystem detection; 0 if unknown.
    uint64_t ino = 0;       ///< Inode number (st_ino) for hardlink dedup; 0 if unknown.
    bool isDir = false;     ///< Whether this entry is a directory.
    bool isSymlink = false; ///< Whether this entry is a symbolic link (from lstat, not followed).
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
