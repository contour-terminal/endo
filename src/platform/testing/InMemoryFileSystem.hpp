// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <initializer_list>
#include <map>
#include <set>
#include <string>

#include <platform/FileSystem.hpp>

namespace endo::platform::testing
{

/// Describes a single filesystem entry for InMemoryFileSystem initialization.
struct FileEntry
{
    std::filesystem::path path;
    std::string content;
    std::filesystem::perms perms = std::filesystem::perms::owner_read | std::filesystem::perms::owner_write;
    bool isExecutable = false; ///< Shorthand for adding owner_exec to perms.
    bool isDirectory = false;  ///< If true, creates a directory instead of a file.
    bool isSymlink = false;    ///< If true, content is treated as symlink target.
};

/// In-memory filesystem implementation for testing.
///
/// Stores files as string contents in a map, supports basic directory operations.
/// Files are keyed by their lexically-normal path string for consistent lookup.
///
/// Usage:
/// @code
/// InMemoryFileSystem fs({
///     { "/home/user/.config/endo/init.endo", "echo hello" },
///     { "/usr/bin/grep", "", { .isExecutable = true } },
///     { "/tmp/data", "", { .isDirectory = true } },
/// });
/// @endcode
class InMemoryFileSystem final: public FileSystem
{
  public:
    InMemoryFileSystem() = default;

    /// Construct with a list of file entries for convenient test setup.
    InMemoryFileSystem(std::initializer_list<FileEntry> entries);

    // Path queries
    [[nodiscard]] bool exists(std::filesystem::path const& path) const override;
    [[nodiscard]] bool isDirectory(std::filesystem::path const& path) const override;
    [[nodiscard]] bool isRegularFile(std::filesystem::path const& path) const override;
    [[nodiscard]] bool isSymlink(std::filesystem::path const& path) const override;
    [[nodiscard]] bool isExecutableFile(std::filesystem::path const& path) const override;
    [[nodiscard]] std::filesystem::path weaklyCanonical(std::filesystem::path const& path) const override;
    [[nodiscard]] std::filesystem::path currentPath() const override;

    // File I/O
    [[nodiscard]] std::expected<std::string, std::string> readFile(
        std::filesystem::path const& path) const override;
    [[nodiscard]] std::expected<void, std::string> writeFile(std::filesystem::path const& path,
                                                             std::string_view content) const override;
    [[nodiscard]] std::expected<void, std::string> appendFile(std::filesystem::path const& path,
                                                              std::string_view content) const override;
    [[nodiscard]] std::unique_ptr<std::istream> openRead(std::filesystem::path const& path) const override;
    [[nodiscard]] std::unique_ptr<std::ostream> openWrite(std::filesystem::path const& path,
                                                          bool append = false) const override;
    [[nodiscard]] std::unique_ptr<std::iostream> openReadWrite(
        std::filesystem::path const& path) const override;

    // Directory ops
    [[nodiscard]] std::expected<void, std::string> createDirectory(
        std::filesystem::path const& path) const override;
    [[nodiscard]] std::expected<void, std::string> createDirectories(
        std::filesystem::path const& path) const override;
    [[nodiscard]] std::expected<bool, std::string> remove(std::filesystem::path const& path) const override;
    [[nodiscard]] std::expected<std::uintmax_t, std::string> removeAll(
        std::filesystem::path const& path) const override;
    [[nodiscard]] std::expected<void, std::string> copyFile(std::filesystem::path const& from,
                                                            std::filesystem::path const& to,
                                                            bool overwrite = false) const override;
    [[nodiscard]] std::expected<void, std::string> rename(std::filesystem::path const& from,
                                                          std::filesystem::path const& to) const override;

    // Directory listing
    [[nodiscard]] std::expected<std::vector<DirectoryEntry>, std::string> listDirectory(
        std::filesystem::path const& path) const override;
    [[nodiscard]] Generator<DirectoryEntry> walkDirectoryRecursive(
        std::filesystem::path path, std::error_code* outError = nullptr) const override;

    // Metadata
    [[nodiscard]] std::expected<std::uintmax_t, std::string> fileSize(
        std::filesystem::path const& path) const override;
    [[nodiscard]] std::expected<std::filesystem::file_time_type, std::string> lastWriteTime(
        std::filesystem::path const& path) const override;
    [[nodiscard]] std::expected<std::filesystem::perms, std::string> permissions(
        std::filesystem::path const& path) const override;
    [[nodiscard]] std::expected<void, std::string> setPermissions(
        std::filesystem::path const& path, std::filesystem::perms perms) const override;

    // Temp files
    [[nodiscard]] std::expected<std::filesystem::path, std::string> createTempFile(
        std::string_view prefix) const override;

    // --- Test helpers ---

    /// Sets the current working directory for the virtual filesystem.
    void setCurrentPath(std::filesystem::path const& path);

    /// Adds a file with the given content and optional permissions.
    void addFile(std::filesystem::path const& path,
                 std::string content,
                 std::filesystem::perms perms = std::filesystem::perms::owner_read
                                                | std::filesystem::perms::owner_write);

    /// Adds an executable file (owner_read | owner_write | owner_exec).
    void addExecutable(std::filesystem::path const& path, std::string content = {});

    /// Adds an empty directory.
    void addDirectory(std::filesystem::path const& path);

    /// Adds a symlink entry (points to target, stored as metadata).
    void addSymlink(std::filesystem::path const& path, std::filesystem::path const& target);

    /// Deny all access to a path (simulate EACCES / EPERM).
    void denyAccess(std::filesystem::path const& path);

  private:
    [[nodiscard]] std::string normalize(std::filesystem::path const& path) const;
    void ensureParentDirectories(std::filesystem::path const& path) const;

    mutable std::map<std::string, std::string> _files;                  ///< path -> content
    mutable std::set<std::string> _directories;                         ///< known directories
    mutable std::map<std::string, std::string> _symlinks;               ///< path -> target
    mutable std::map<std::string, std::filesystem::perms> _permissions; ///< path -> permissions
    std::set<std::string> _deniedPaths;                                 ///< paths that simulate EACCES
    std::filesystem::path _currentPath = "/";
    mutable int _tempCounter = 0;
};

} // namespace endo::platform::testing

namespace endo
{
using endo::platform::testing::InMemoryFileSystem;
} // namespace endo
