// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <platform/Generator.hpp>

namespace endo::platform
{

/// Abstract interface for filesystem operations.
///
/// This interface abstracts all filesystem I/O, enabling unit-testing of
/// shell builtins and subsystems in isolation via InMemoryFileSystem.
class FileSystem
{
  public:
    virtual ~FileSystem() = default;

    // Path queries
    [[nodiscard]] virtual bool exists(std::filesystem::path const& path) const = 0;
    [[nodiscard]] virtual bool isDirectory(std::filesystem::path const& path) const = 0;
    [[nodiscard]] virtual bool isRegularFile(std::filesystem::path const& path) const = 0;
    [[nodiscard]] virtual bool isSymlink(std::filesystem::path const& path) const = 0;

    /// Tests whether @p path names a file the shell can execute.
    ///
    /// Returns true for regular files and symlinks to them, and — on Windows — also for
    /// App Execution Alias reparse points (e.g. `winget`, Microsoft Store `python`). These
    /// are zero-byte `IO_REPARSE_TAG_APPEXECLINK` reparse points that are neither regular
    /// files nor symlinks and cannot be opened or followed through normal filesystem APIs,
    /// yet they must stay discoverable on `PATH` so that `CreateProcessW` can launch them —
    /// matching how `cmd.exe` and PowerShell resolve such commands. On POSIX the file must
    /// additionally carry an execute permission bit. Directories always return false.
    ///
    /// @param path The path to test.
    /// @return True if @p path is a runnable executable file, false otherwise.
    [[nodiscard]] virtual bool isExecutableFile(std::filesystem::path const& path) const = 0;
    [[nodiscard]] virtual std::filesystem::path weaklyCanonical(std::filesystem::path const& path) const = 0;
    [[nodiscard]] virtual std::filesystem::path currentPath() const = 0;

    // File I/O
    [[nodiscard]] virtual std::expected<std::string, std::string> readFile(
        std::filesystem::path const& path) const = 0;
    [[nodiscard]] virtual std::expected<void, std::string> writeFile(std::filesystem::path const& path,
                                                                     std::string_view content) const = 0;
    [[nodiscard]] virtual std::expected<void, std::string> appendFile(std::filesystem::path const& path,
                                                                      std::string_view content) const = 0;
    [[nodiscard]] virtual std::unique_ptr<std::istream> openRead(std::filesystem::path const& path) const = 0;
    [[nodiscard]] virtual std::unique_ptr<std::ostream> openWrite(std::filesystem::path const& path,
                                                                  bool append = false) const = 0;
    [[nodiscard]] virtual std::unique_ptr<std::iostream> openReadWrite(
        std::filesystem::path const& path) const = 0;

    // Directory ops
    /// Creates a single directory. Fails if the parent does not exist.
    [[nodiscard]] virtual std::expected<void, std::string> createDirectory(
        std::filesystem::path const& path) const = 0;
    /// Creates a directory and all missing parent directories.
    [[nodiscard]] virtual std::expected<void, std::string> createDirectories(
        std::filesystem::path const& path) const = 0;
    [[nodiscard]] virtual std::expected<bool, std::string> remove(
        std::filesystem::path const& path) const = 0;
    [[nodiscard]] virtual std::expected<std::uintmax_t, std::string> removeAll(
        std::filesystem::path const& path) const = 0;
    [[nodiscard]] virtual std::expected<void, std::string> copyFile(std::filesystem::path const& from,
                                                                    std::filesystem::path const& to,
                                                                    bool overwrite = false) const = 0;
    [[nodiscard]] virtual std::expected<void, std::string> rename(std::filesystem::path const& from,
                                                                  std::filesystem::path const& to) const = 0;

    // Directory listing
    struct DirectoryEntry
    {
        std::filesystem::path path;
        bool isDirectory = false;
        bool isRegularFile = false;
        bool isSymlink = false;
        /// Depth below the walk root: 1 for a direct child, 2 for a grandchild, etc.
        /// 0 for entries that are not the product of a recursive walk (e.g. listDirectory).
        int depth = 0;
    };

    [[nodiscard]] virtual std::expected<std::vector<DirectoryEntry>, std::string> listDirectory(
        std::filesystem::path const& path) const = 0;

    /// Materializes @ref walkDirectoryRecursive into a vector (parents before their contents).
    ///
    /// Implemented once here in terms of the lazy walk, so backends only provide the coroutine.
    /// Prefer @ref walkDirectoryRecursive directly for large trees or interruptible consumers;
    /// this convenience eagerly buffers the whole tree.
    ///
    /// @param path Root directory to list recursively.
    /// @return Every entry under @p path, or an error string if enumeration failed.
    [[nodiscard]] std::expected<std::vector<DirectoryEntry>, std::string> listDirectoryRecursive(
        std::filesystem::path const& path) const
    {
        std::error_code ec;
        auto entries = std::vector<DirectoryEntry> {};
        for (auto const& entry: walkDirectoryRecursive(path, &ec))
            entries.push_back(entry);
        if (ec)
            return std::unexpected(
                std::format("Cannot recursively list directory '{}': {}", path.string(), ec.message()));
        return entries;
    }

    /// Lazily walks @p path recursively, yielding each entry as it is discovered
    /// (parents before their contents) without materializing the whole tree.
    /// Permission-denied subtrees are skipped.
    ///
    /// Iterating the returned generator drives the walk one entry at a time;
    /// abandoning it (e.g. `break`-ing out of the loop) destroys the suspended
    /// coroutine and stops the walk. That single-step laziness is what lets
    /// long-running consumers (find, cp, rm, grep) poll for Ctrl+C between
    /// entries and abort promptly.
    ///
    /// @param path Root directory to walk. Taken BY VALUE: the implementation is
    ///             a coroutine, so a by-reference parameter would dangle in the
    ///             coroutine frame once the caller's argument expires.
    /// @param outError Optional out-parameter (taken by pointer, not reference,
    ///             to keep the coroutine free of reference parameters; it must
    ///             outlive the returned generator). When non-null, it is set to
    ///             the error that aborted the walk — a non-existent/non-directory
    ///             root, or an iteration error (other than skipped
    ///             permission-denied subtrees). Destructive callers (cp/mv/rm)
    ///             pass it so they can avoid acting on a partial enumeration;
    ///             read-only callers (find/grep) leave it null and tolerate a
    ///             partial walk. It is left untouched on a fully successful walk.
    /// @return A generator of entries. A non-existent or non-directory root, or
    ///         an unreadable root, simply yields nothing. The owning FileSystem
    ///         must outlive the returned generator.
    [[nodiscard]] virtual Generator<DirectoryEntry> walkDirectoryRecursive(
        std::filesystem::path path, std::error_code* outError = nullptr) const = 0;

    // Metadata
    [[nodiscard]] virtual std::expected<std::uintmax_t, std::string> fileSize(
        std::filesystem::path const& path) const = 0;
    [[nodiscard]] virtual std::expected<std::filesystem::file_time_type, std::string> lastWriteTime(
        std::filesystem::path const& path) const = 0;
    [[nodiscard]] virtual std::expected<std::filesystem::perms, std::string> permissions(
        std::filesystem::path const& path) const = 0;
    [[nodiscard]] virtual std::expected<void, std::string> setPermissions(
        std::filesystem::path const& path, std::filesystem::perms perms) const = 0;

    // Temp files
    [[nodiscard]] virtual std::expected<std::filesystem::path, std::string> createTempFile(
        std::string_view prefix) const = 0;
};

} // namespace endo::platform

namespace endo
{
using endo::platform::FileSystem;
} // namespace endo
