// SPDX-License-Identifier: Apache-2.0
#include "WindowsFileInfoProvider.hpp"

#if defined(_WIN32)

    #include <algorithm>
    #include <chrono>
    #include <filesystem>
    #include <string>
    #include <string_view>
    #include <vector>

    #include <platform/GlobMatch.hpp>
    #include <platform/PathUtils.hpp>

namespace endo::platform
{

namespace
{

    namespace fs = std::filesystem;

    /// Losslessly renders a path as a UTF-8 std::string.
    ///
    /// path::string() converts to the process's narrow (ANSI) code page and throws
    /// std::system_error on any character the code page cannot represent — fatal when
    /// listing a directory that holds such a name. path::u8string() encodes the full
    /// Unicode path as UTF-8 and never throws, so we use it and reinterpret the bytes.
    /// @param p The path to convert.
    /// @return The path encoded as UTF-8.
    [[nodiscard]] std::string toUtf8(fs::path const& p)
    {
        auto const u8 = p.u8string();
        return std::string { reinterpret_cast<char const*>(u8.data()), u8.size() };
    }

    /// Returns @p dir as an absolute, UTF-8, forward-slash normalized directory path.
    ///
    /// Not platform::absoluteDirectory(): that goes through normalizePath(fs::path), which uses
    /// generic_string() and so narrows to the ANSI code page — throwing on any directory name the
    /// code page cannot represent. Resolved once per listing, as the shared helper is.
    ///
    /// @param dir Directory to resolve; empty is treated as the working directory.
    /// @return The absolute directory, or an empty string if it could not be resolved.
    [[nodiscard]] std::string absoluteDirectoryUtf8(fs::path const& dir)
    {
        std::error_code ec;
        auto const resolved = fs::absolute(dir.empty() ? fs::path { "." } : dir, ec);
        if (ec)
            return {};
        return normalizePath(toUtf8(resolved.lexically_normal()));
    }

    /// Populates a FileEntry from a filesystem directory_entry.
    /// @param dirEntry The entry to inspect.
    /// @param absoluteParent Absolute, normalized directory holding the entry, used to build
    ///                       FileEntry::path. Empty leaves the path empty.
    /// @param fileEntry Output entry to populate.
    /// @return true on success, false if the entry could not be stat'd.
    [[nodiscard]] bool statEntry(fs::directory_entry const& dirEntry,
                                 std::string_view absoluteParent,
                                 FileEntry& fileEntry)
    {
        std::error_code ec;

        fileEntry.name = toUtf8(dirEntry.path().filename());
        if (!absoluteParent.empty())
            fileEntry.path = joinPath(absoluteParent, fileEntry.name);

        // status() follows reparse points. App Execution Aliases (winget, Microsoft Store
        // python, …) are reparse points that cannot be opened or followed through normal
        // filesystem APIs, so the follow fails. Fall back to the non-following
        // symlink_status() so such entries are still listed — matching `cmd.exe` / `dir`.
        auto status = dirEntry.status(ec);
        if (ec)
        {
            ec.clear();
            status = dirEntry.symlink_status(ec);
            if (ec)
                return false;
        }

        fileEntry.isDir = fs::is_directory(status);
        fileEntry.isSymlink = fs::is_symlink(dirEntry.symlink_status(ec));
        ec.clear();
        if (fileEntry.isSymlink)
        {
            // Read the link target verbatim. Reading a reparse point can fail without the
            // required privilege; on failure leave the target empty (graceful degradation).
            auto const target = fs::read_symlink(dirEntry.path(), ec);
            if (!ec)
                fileEntry.symlinkTarget = toUtf8(target);
            ec.clear();
        }
        fileEntry.size = 0;

        // Windows std::filesystem does not expose st_blocks/st_dev/st_ino. Leave the
        // documented sentinels (blocks = -1, dev = ino = 0); disk-usage callers fall
        // back to apparent size and disable hardlink dedup / cross-device detection.
        fileEntry.blocks = -1;
        fileEntry.dev = 0;
        fileEntry.ino = 0;

        if (!fileEntry.isDir)
        {
            auto const fileSize = dirEntry.file_size(ec);
            if (!ec)
                fileEntry.size = static_cast<int64_t>(fileSize);
            else
                ec.clear();
        }

        auto const perms = status.permissions();
        int mode = 0;
        if ((perms & fs::perms::owner_read) != fs::perms::none)
            mode |= 0444;
        if ((perms & fs::perms::owner_write) != fs::perms::none)
            mode |= 0222;
        if ((perms & fs::perms::owner_exec) != fs::perms::none)
            mode |= 0111;
        if (fileEntry.isDir)
            mode |= 0111;
        fileEntry.mode = mode;

        auto const lastWrite = dirEntry.last_write_time(ec);
        if (!ec)
        {
            auto const sctp = std::chrono::clock_cast<std::chrono::system_clock>(lastWrite);
            fileEntry.mtime =
                std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count();
        }
        else
        {
            fileEntry.mtime = 0;
        }

        return true;
    }

} // namespace

std::vector<FileEntry> WindowsFileInfoProvider::listDirectory(std::string const& path) const
{
    std::vector<FileEntry> result;
    std::error_code ec;

    // Case 1: Glob pattern — check first (pure string scan, avoids unnecessary syscalls).
    if (endo::containsGlobChars(path))
    {
        auto const patternPath = fs::path(path);
        auto parentDir = patternPath.parent_path();
        auto const filePattern = patternPath.filename().string();

        if (parentDir.empty())
            parentDir = ".";

        auto const absoluteParent = absoluteDirectoryUtf8(parentDir);

        for (auto const& entry: fs::directory_iterator(parentDir, ec))
        {
            if (ec)
                break;

            auto const filename = entry.path().filename().string();
            if (endo::globMatchFilename(filename, filePattern))
            {
                FileEntry fileEntry {};
                if (statEntry(entry, absoluteParent, fileEntry))
                    result.push_back(std::move(fileEntry));
            }
        }

        std::ranges::sort(result, {}, &FileEntry::name);
        return result;
    }

    // Case 2: Directory path — enumerate contents.
    auto const dir = fs::path(path);
    if (fs::is_directory(dir, ec) && !ec)
    {
        auto const absoluteParent = absoluteDirectoryUtf8(dir);

        for (auto const& entry: fs::directory_iterator(dir, ec))
        {
            if (ec)
                break;

            FileEntry fileEntry {};
            if (statEntry(entry, absoluteParent, fileEntry))
                result.push_back(std::move(fileEntry));
        }

        std::ranges::sort(result, {}, &FileEntry::name);
        return result;
    }

    // Case 3: Single file path — stat and return one entry.
    if (auto const dirEntry = fs::directory_entry(dir, ec); !ec)
    {
        FileEntry fileEntry {};
        if (statEntry(dirEntry, absoluteDirectoryUtf8(dir.parent_path()), fileEntry))
            result.push_back(std::move(fileEntry));
    }

    return result;
}

} // namespace endo::platform

#endif // _WIN32
