// SPDX-License-Identifier: Apache-2.0
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <ranges>
#include <sstream>
#include <string>
#include <system_error>

#include <platform/NativeFileSystem.hpp>
#include <platform/PathUtils.hpp>

#if !defined(_WIN32)
    #include <unistd.h>
#else
    #include <io.h>
    #include <windows.h>
#endif

namespace fs = std::filesystem;

namespace endo::platform
{

NativeFileSystem& NativeFileSystem::instance()
{
    static NativeFileSystem instance;
    return instance;
}

bool NativeFileSystem::exists(fs::path const& path) const
{
    std::error_code ec;
    return fs::exists(path, ec);
}

bool NativeFileSystem::isDirectory(fs::path const& path) const
{
    std::error_code ec;
    return fs::is_directory(path, ec);
}

bool NativeFileSystem::isRegularFile(fs::path const& path) const
{
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}

bool NativeFileSystem::isSymlink(fs::path const& path) const
{
    std::error_code ec;
    return fs::is_symlink(path, ec);
}

bool NativeFileSystem::isExecutableFile(fs::path const& path) const
{
#if defined(_WIN32)
    // GetFileAttributesW does not follow reparse points, so it succeeds for App Execution
    // Alias entries (winget, Store python, …) that CreateFileW/std::filesystem cannot open.
    // Any existing non-directory entry is a runnable candidate; PATHEXT already restricts
    // bare-name search to executable extensions, matching cmd.exe / PowerShell semantics.
    auto const attrs = GetFileAttributesW(path.wstring().c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
    std::error_code ec;
    if (!fs::is_regular_file(path, ec) && !fs::is_symlink(path, ec))
        return false;

    auto const status = fs::status(path, ec);
    if (ec)
        return false;

    auto const perms = status.permissions();
    return (perms & fs::perms::owner_exec) != fs::perms::none
           || (perms & fs::perms::group_exec) != fs::perms::none
           || (perms & fs::perms::others_exec) != fs::perms::none;
#endif
}

fs::path NativeFileSystem::weaklyCanonical(fs::path const& path) const
{
    std::error_code ec;
    auto result = fs::weakly_canonical(path, ec);
    if (ec)
        return path;
    return result;
}

fs::path NativeFileSystem::currentPath() const
{
    return fs::current_path();
}

std::expected<std::string, std::string> NativeFileSystem::readFile(fs::path const& path) const
{
    auto ifs = std::ifstream(path, std::ios::binary);
    if (!ifs)
        return std::unexpected(std::format("Cannot open file: {}", path.string()));

    auto oss = std::ostringstream {};
    oss << ifs.rdbuf();
    if (ifs.bad())
        return std::unexpected(std::format("Error reading file: {}", path.string()));
    return oss.str();
}

std::expected<void, std::string> NativeFileSystem::writeFile(fs::path const& path,
                                                             std::string_view content) const
{
    auto ofs = std::ofstream(path, std::ios::binary | std::ios::trunc);
    if (!ofs)
        return std::unexpected(std::format("Cannot open file for writing: {}", path.string()));

    ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!ofs)
        return std::unexpected(std::format("Error writing file: {}", path.string()));
    return {};
}

std::expected<void, std::string> NativeFileSystem::appendFile(fs::path const& path,
                                                              std::string_view content) const
{
    auto ofs = std::ofstream(path, std::ios::binary | std::ios::app);
    if (!ofs)
        return std::unexpected(std::format("Cannot open file for appending: {}", path.string()));

    ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!ofs)
        return std::unexpected(std::format("Error appending to file: {}", path.string()));
    return {};
}

std::unique_ptr<std::istream> NativeFileSystem::openRead(fs::path const& path) const
{
    auto stream = std::make_unique<std::ifstream>(path, std::ios::binary);
    if (!*stream)
        return nullptr;
    return stream;
}

std::unique_ptr<std::ostream> NativeFileSystem::openWrite(fs::path const& path, bool append) const
{
    auto const mode = std::ios::binary | (append ? std::ios::app : std::ios::trunc);
    auto stream = std::make_unique<std::ofstream>(path, mode);
    if (!*stream)
        return nullptr;
    return stream;
}

std::unique_ptr<std::iostream> NativeFileSystem::openReadWrite(fs::path const& path) const
{
    auto stream = std::make_unique<std::fstream>(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!*stream)
        return nullptr;
    return stream;
}

std::expected<void, std::string> NativeFileSystem::createDirectory(fs::path const& path) const
{
    std::error_code ec;
    if (!fs::create_directory(path, ec) || ec)
        return std::unexpected(std::format("Cannot create directory '{}': {}",
                                           path.string(),
                                           ec ? ec.message() : "No such file or directory"));
    return {};
}

std::expected<void, std::string> NativeFileSystem::createDirectories(fs::path const& path) const
{
    std::error_code ec;
    fs::create_directories(path, ec);
    if (ec)
        return std::unexpected(
            std::format("Cannot create directories '{}': {}", path.string(), ec.message()));
    return {};
}

std::expected<bool, std::string> NativeFileSystem::remove(fs::path const& path) const
{
    std::error_code ec;
    auto const result = fs::remove(path, ec);
    if (ec)
        return std::unexpected(std::format("Cannot remove '{}': {}", path.string(), ec.message()));
    return result;
}

std::expected<std::uintmax_t, std::string> NativeFileSystem::removeAll(fs::path const& path) const
{
    std::error_code ec;
    auto const result = fs::remove_all(path, ec);
    if (ec)
        return std::unexpected(std::format("Cannot remove '{}': {}", path.string(), ec.message()));
    return result;
}

std::expected<void, std::string> NativeFileSystem::copyFile(fs::path const& from,
                                                            fs::path const& to,
                                                            bool overwrite) const
{
    std::error_code ec;
    auto const opts = overwrite ? fs::copy_options::overwrite_existing : fs::copy_options::none;
    fs::copy_file(from, to, opts, ec);
    if (ec)
        return std::unexpected(
            std::format("Cannot copy '{}' to '{}': {}", from.string(), to.string(), ec.message()));
    return {};
}

namespace
{

    /// Renames @p from to @p to in two hops via a unique temporary name in the
    /// destination's parent directory.
    ///
    /// Used to perform a lettercase-only rename (e.g. `foo` -> `Foo`) on case-insensitive
    /// filesystems, where a direct rename is rejected because source and destination
    /// resolve to the same entry. A free temporary name is found in the destination's
    /// directory, the entry is moved there, then moved on to its final name. If the second
    /// hop fails the entry is rolled back to its original name so it is never stranded under
    /// the temporary.
    ///
    /// @param from The existing source entry.
    /// @param to The desired destination, in the same directory and differing only in case.
    /// @param ec Set to the error that aborted the operation; cleared on success.
    /// @return True on success, false otherwise (with @p ec describing the failure).
    [[nodiscard]] bool renameViaTemporary(fs::path const& from, fs::path const& to, std::error_code& ec)
    {
        auto const parent = to.parent_path();
        auto const baseName = to.filename().string();
        for (auto const attempt: std::views::iota(0, 1000))
        {
            auto const candidate = parent / (baseName + ".endo-recase-" + std::to_string(attempt));
            if (fs::exists(candidate, ec))
                continue;
            ec.clear();

            fs::rename(from, candidate, ec);
            if (ec)
                return false;

            fs::rename(candidate, to, ec);
            if (!ec)
                return true;

            // Second hop failed: restore the original name so the entry is not stranded
            // under the temporary. Best-effort — the reported error stays the second hop's.
            std::error_code rollbackError;
            auto const& originalName = from;
            fs::rename(candidate, originalName, rollbackError);
            return false;
        }
        ec = std::make_error_code(std::errc::file_exists);
        return false;
    }

} // namespace

std::expected<void, std::string> NativeFileSystem::rename(fs::path const& from, fs::path const& to) const
{
    std::error_code ec;
    fs::rename(from, to, ec);
    if (!ec)
        return {};

    // On case-insensitive filesystems (Windows, the default macOS volume format) a
    // rename that changes only the lettercase of the final path component — e.g.
    // `foo` -> `Foo` — can be rejected because source and destination resolve to the
    // same entry. Perform such a recase in two hops through a temporary name so the
    // lettercase change still takes effect.
    if (isCaseOnlyRename(from, to))
    {
        std::error_code recaseError;
        if (renameViaTemporary(from, to, recaseError))
            return {};
    }

    return std::unexpected(
        std::format("Cannot rename '{}' to '{}': {}", from.string(), to.string(), ec.message()));
}

std::expected<std::vector<FileSystem::DirectoryEntry>, std::string> NativeFileSystem::listDirectory(
    fs::path const& path) const
{
    std::error_code ec;
    auto entries = std::vector<DirectoryEntry> {};
    auto it = fs::directory_iterator(path, ec);
    auto const end = fs::directory_iterator {};
    // C-style loop is intentional here, as in walkDirectoryRecursive(): a range-based for
    // would advance through the throwing operator++, so a mid-walk readdir failure would
    // escape this std::expected-returning function as a filesystem_error.
    for (; !ec && it != end; it.increment(ec))
    {
        auto const& entry = *it;
        // error_code overloads: the throwing ones escape as filesystem_error whenever an
        // entry cannot be stat'ed (EACCES on a readable-but-not-searchable directory, or a
        // filesystem that reports DT_UNKNOWN), which would abort listing an otherwise
        // perfectly readable directory. An unknown entry is reported as neither.
        std::error_code entryEc;
        entries.push_back(DirectoryEntry {
            .path = entry.path(),
            .isDirectory = entry.is_directory(entryEc),
            .isRegularFile = entry.is_regular_file(entryEc),
            .isSymlink = entry.is_symlink(entryEc),
        });
    }
    if (ec)
        return std::unexpected(std::format("Cannot list directory '{}': {}", path.string(), ec.message()));
    return entries;
}

Generator<FileSystem::DirectoryEntry> NativeFileSystem::walkDirectoryRecursive(
    fs::path path, std::error_code* outError) const
{
    // recursive_directory_iterator reads directories lazily as it advances, so
    // co_yield-ing per entry streams the walk to the consumer one entry at a
    // time. The error_code overload is used so a bad/unreadable root or a
    // mid-walk error yields nothing/partial rather than throwing out of the
    // coroutine; the error is surfaced via outError (when provided) so
    // destructive callers can avoid acting on an incomplete enumeration.
    std::error_code ec;
    auto it = fs::recursive_directory_iterator(path, fs::directory_options::skip_permission_denied, ec);
    auto const end = fs::recursive_directory_iterator {};
    // C-style loop is intentional here: a range-based for would use the throwing operator++,
    // whereas we must thread an error_code through the non-throwing increment(ec) overload.
    for (; !ec && it != end; it.increment(ec))
    {
        auto const& entry = *it;
        // error_code overloads, so an unstattable entry is reported as neither a directory
        // nor a regular file instead of throwing filesystem_error out of the coroutine.
        // `entryEc` is deliberately separate from `ec`, which drives the loop.
        std::error_code entryEc;
        co_yield DirectoryEntry {
            .path = entry.path(),
            .isDirectory = entry.is_directory(entryEc),
            .isRegularFile = entry.is_regular_file(entryEc),
            .isSymlink = entry.is_symlink(entryEc),
            // recursive_directory_iterator::depth() is 0 for a direct child; +1 to match the
            // walk-root-relative convention (direct child = depth 1) consumers expect.
            .depth = it.depth() + 1,
        };
    }
    if (ec && outError != nullptr)
        *outError = ec;
}

std::expected<std::uintmax_t, std::string> NativeFileSystem::fileSize(fs::path const& path) const
{
    std::error_code ec;
    auto const size = fs::file_size(path, ec);
    if (ec)
        return std::unexpected(std::format("Cannot get file size '{}': {}", path.string(), ec.message()));
    return size;
}

std::expected<fs::file_time_type, std::string> NativeFileSystem::lastWriteTime(fs::path const& path) const
{
    std::error_code ec;
    auto const time = fs::last_write_time(path, ec);
    if (ec)
        return std::unexpected(
            std::format("Cannot get last write time '{}': {}", path.string(), ec.message()));
    return time;
}

std::expected<fs::perms, std::string> NativeFileSystem::permissions(fs::path const& path) const
{
    std::error_code ec;
    auto const status = fs::status(path, ec);
    if (ec)
        return std::unexpected(std::format("Cannot get permissions '{}': {}", path.string(), ec.message()));
    return status.permissions();
}

std::expected<void, std::string> NativeFileSystem::setPermissions(fs::path const& path, fs::perms perms) const
{
    std::error_code ec;
    fs::permissions(path, perms, ec);
    if (ec)
        return std::unexpected(std::format("Cannot set permissions '{}': {}", path.string(), ec.message()));
    return {};
}

std::expected<fs::path, std::string> NativeFileSystem::createTempFile(std::string_view prefix) const
{
    auto const tmpDir = fs::temp_directory_path();
    auto const pattern = std::format("{}{}_XXXXXX", tmpDir.string() + "/", prefix);
    auto templateStr = std::string(pattern);

#if defined(_WIN32)
    if (_mktemp_s(templateStr.data(), templateStr.size() + 1) != 0)
        return std::unexpected("Failed to create temporary file name");
    // Create the file
    auto ofs = std::ofstream(templateStr);
    if (!ofs)
        return std::unexpected("Failed to create temporary file");
    ofs.close();
#else
    auto const fd = mkstemp(templateStr.data());
    if (fd == -1)
        return std::unexpected(std::format("Failed to create temporary file: {}", strerror(errno)));
    ::close(fd);
#endif
    return fs::path(templateStr);
}

} // namespace endo::platform
