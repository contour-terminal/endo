// SPDX-License-Identifier: Apache-2.0
#include "LinuxFileInfoProvider.hpp"

#include <algorithm>
#include <filesystem>
#include <string>

#include <sys/stat.h>

#include <platform/GlobMatch.hpp>
#include <platform/PathUtils.hpp>

namespace endo::platform
{

namespace
{

    namespace fs = std::filesystem;

    /// Populates a FileEntry from a filesystem path using a raw lstat(2).
    ///
    /// lstat() does not follow symlinks, so dangling links are reported as their own
    /// (link) entry rather than silently dropped — matching `ls`. Using the raw stat
    /// (instead of std::filesystem) lets us capture the allocated block count
    /// (st_blocks), device id (st_dev), and inode (st_ino) that true disk-usage
    /// accounting, hardlink dedup, and cross-filesystem detection require.
    ///
    /// @param fullPath Absolute or relative path to stat.
    /// @param name     The entry's display name (filename component).
    /// @param absoluteParent Absolute, normalized directory holding the entry, used to build
    ///                       FileEntry::path. Empty leaves the path empty.
    /// @param entry    Output entry to populate.
    /// @return true on success, false if the path could not be stat'd.
    [[nodiscard]] bool statEntry(std::string const& fullPath,
                                 std::string name,
                                 std::string_view absoluteParent,
                                 FileEntry& entry)
    {
        struct stat st {};
        if (::lstat(fullPath.c_str(), &st) != 0)
            return false;

        entry.name = std::move(name);
        if (!absoluteParent.empty())
        {
            entry.path = std::string { absoluteParent };
            if (!entry.path.ends_with('/'))
                entry.path += '/';
            entry.path += entry.name;
        }
        entry.isSymlink = S_ISLNK(st.st_mode);
        if (entry.isSymlink)
        {
            // Read the link target verbatim (do not canonicalize), matching `ls -l`.
            // A failure (e.g. permissions) leaves symlinkTarget empty, which is acceptable.
            std::error_code ec;
            auto const target = fs::read_symlink(fullPath, ec);
            if (!ec)
                entry.symlinkTarget = target.string();
        }
        entry.mode = static_cast<int64_t>(st.st_mode) & 0777;
        entry.mtime = static_cast<int64_t>(st.st_mtime);
        entry.dev = static_cast<uint64_t>(st.st_dev);
        entry.ino = static_cast<uint64_t>(st.st_ino);
        entry.blocks = static_cast<int64_t>(st.st_blocks);

        // For a symlink, lstat reports the link's own metadata. Whether the target is a
        // directory is intentionally not resolved here (the link is not followed), so a
        // symlink is never treated as a directory for traversal purposes.
        entry.isDir = S_ISDIR(st.st_mode);
        entry.size = static_cast<int64_t>(st.st_size);

        return true;
    }

    /// Returns @p dir as an absolute, forward-slash normalized directory path.
    ///
    /// Resolved once per listing rather than once per entry: fs::absolute() consults the
    /// process working directory, so calling it for every file would add a syscall per entry
    /// on a hot path.
    ///
    /// @param dir Directory to resolve; empty is treated as the working directory.
    /// @return The absolute directory, or an empty string if it could not be resolved.
    [[nodiscard]] std::string absoluteDirectory(fs::path const& dir)
    {
        std::error_code ec;
        auto const absolute = fs::absolute(dir.empty() ? fs::path { "." } : dir, ec);
        if (ec)
            return {};
        return normalizePath(absolute.lexically_normal());
    }

} // namespace

std::vector<FileEntry> LinuxFileInfoProvider::listDirectory(std::string const& path) const
{
    std::vector<FileEntry> entries;
    std::error_code ec;

    // Case 1: Glob pattern — check first (pure string scan, avoids unnecessary syscalls).
    if (endo::containsGlobChars(path))
    {
        auto const patternPath = fs::path(path);
        auto parentDir = patternPath.parent_path();
        auto const filePattern = patternPath.filename().string();

        if (parentDir.empty())
            parentDir = ".";

        auto const absoluteParent = absoluteDirectory(parentDir);

        for (auto const& dirEntry: fs::directory_iterator(parentDir, ec))
        {
            if (ec)
                break;

            auto filename = dirEntry.path().filename().string();
            if (endo::globMatchFilename(filename, filePattern))
            {
                FileEntry entry {};
                if (statEntry(dirEntry.path().string(), std::move(filename), absoluteParent, entry))
                    entries.push_back(std::move(entry));
            }
        }

        std::ranges::sort(entries, {}, &FileEntry::name);
        return entries;
    }

    // Case 2: Directory path — enumerate contents.
    if (fs::is_directory(path, ec) && !ec)
    {
        auto const absoluteParent = absoluteDirectory(path);

        for (auto const& dirEntry: fs::directory_iterator(path, ec))
        {
            if (ec)
                break;

            FileEntry entry {};
            if (statEntry(
                    dirEntry.path().string(), dirEntry.path().filename().string(), absoluteParent, entry))
                entries.push_back(std::move(entry));
        }

        std::ranges::sort(entries, {}, &FileEntry::name);
        return entries;
    }

    // Case 3: Single file path — stat and return one entry.
    {
        auto const single = fs::path(path);
        FileEntry entry {};
        if (statEntry(path, single.filename().string(), absoluteDirectory(single.parent_path()), entry))
            entries.push_back(std::move(entry));
    }

    return entries;
}

} // namespace endo::platform
