// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

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
    [[nodiscard]] virtual std::filesystem::path weaklyCanonical(std::filesystem::path const& path) const = 0;
    [[nodiscard]] virtual std::filesystem::path currentPath() const = 0;

    // File I/O
    [[nodiscard]] virtual std::expected<std::string, std::string> readFile(
        std::filesystem::path const& path) const = 0;
    [[nodiscard]] virtual std::expected<void, std::string> writeFile(
        std::filesystem::path const& path, std::string_view content) const = 0;
    [[nodiscard]] virtual std::expected<void, std::string> appendFile(
        std::filesystem::path const& path, std::string_view content) const = 0;
    [[nodiscard]] virtual std::unique_ptr<std::istream> openRead(
        std::filesystem::path const& path) const = 0;
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
    [[nodiscard]] virtual std::expected<void, std::string> copyFile(
        std::filesystem::path const& from, std::filesystem::path const& to,
        bool overwrite = false) const = 0;
    [[nodiscard]] virtual std::expected<void, std::string> rename(
        std::filesystem::path const& from, std::filesystem::path const& to) const = 0;

    // Directory listing
    struct DirectoryEntry
    {
        std::filesystem::path path;
        bool isDirectory = false;
        bool isRegularFile = false;
        bool isSymlink = false;
    };
    [[nodiscard]] virtual std::expected<std::vector<DirectoryEntry>, std::string> listDirectory(
        std::filesystem::path const& path) const = 0;
    [[nodiscard]] virtual std::expected<std::vector<DirectoryEntry>, std::string> listDirectoryRecursive(
        std::filesystem::path const& path) const = 0;

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
