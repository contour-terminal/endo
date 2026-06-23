// SPDX-License-Identifier: Apache-2.0
#include "LinuxFileInfoProvider.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>

#include <platform/GlobMatch.hpp>

namespace endo::platform
{

namespace
{

    namespace fs = std::filesystem;

    /// Populates a FileEntry from a filesystem directory_entry.
    /// @return true on success, false if the entry could not be stat'd.
    [[nodiscard]] bool statEntry(fs::directory_entry const& dirEntry, FileEntry& entry)
    {
        std::error_code ec;

        entry.name = dirEntry.path().filename().string();

        // status() follows symlinks, so a dangling symlink (its target was removed) fails to
        // resolve. Fall back to the non-following symlink_status() so broken links are still
        // listed rather than silently dropped — matching `ls`.
        auto status = dirEntry.status(ec);
        if (ec)
        {
            ec.clear();
            status = dirEntry.symlink_status(ec);
            if (ec)
                return false;
        }

        entry.isDir = fs::is_directory(status);
        entry.mode = static_cast<int64_t>(status.permissions()) & 0777;

        entry.size = 0;
        if (!entry.isDir)
        {
            auto const fileSize = dirEntry.file_size(ec);
            if (!ec)
                entry.size = static_cast<int64_t>(fileSize);
            else
                ec.clear();
        }

        auto const lwt = dirEntry.last_write_time(ec);
        if (!ec)
        {
#if defined(__APPLE__)
            // macOS libc++ lacks std::chrono::clock_cast; file_clock uses the POSIX epoch.
            entry.mtime = std::chrono::duration_cast<std::chrono::seconds>(lwt.time_since_epoch()).count();
#else
            auto const sysTime = std::chrono::clock_cast<std::chrono::system_clock>(lwt);
            entry.mtime =
                std::chrono::duration_cast<std::chrono::seconds>(sysTime.time_since_epoch()).count();
#endif
        }
        else
        {
            entry.mtime = 0;
            ec.clear();
        }

        return true;
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

        for (auto const& dirEntry: fs::directory_iterator(parentDir, ec))
        {
            if (ec)
                break;

            auto const filename = dirEntry.path().filename().string();
            if (endo::globMatchFilename(filename, filePattern))
            {
                FileEntry entry {};
                if (statEntry(dirEntry, entry))
                    entries.push_back(std::move(entry));
            }
        }

        std::ranges::sort(entries, {}, &FileEntry::name);
        return entries;
    }

    // Case 2: Directory path — enumerate contents.
    if (fs::is_directory(path, ec) && !ec)
    {
        for (auto const& dirEntry: fs::directory_iterator(path, ec))
        {
            if (ec)
                break;

            FileEntry entry {};
            if (statEntry(dirEntry, entry))
                entries.push_back(std::move(entry));
        }

        std::ranges::sort(entries, {}, &FileEntry::name);
        return entries;
    }

    // Case 3: Single file path — stat and return one entry.
    if (auto const dirEntry = fs::directory_entry(fs::path(path), ec); !ec)
    {
        FileEntry entry {};
        if (statEntry(dirEntry, entry))
            entries.push_back(std::move(entry));
    }

    return entries;
}

} // namespace endo::platform
