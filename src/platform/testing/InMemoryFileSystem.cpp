// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <format>
#include <sstream>

#include <platform/testing/InMemoryFileSystem.hpp>

namespace endo::platform::testing
{

namespace
{
    // Custom streambuf that syncs back to the InMemoryFileSystem's file map on flush/destruction.
    class MemoryOutputBuf final: public std::streambuf
    {
      public:
        MemoryOutputBuf(std::string* target, bool append): _target(target)
        {
            if (!append)
                _target->clear();
            _writePos = _target->size();
        }

      protected:
        std::streamsize xsputn(char const* s, std::streamsize n) override
        {
            _target->append(s, static_cast<size_t>(n));
            _writePos += static_cast<size_t>(n);
            return n;
        }

        int_type overflow(int_type ch) override
        {
            if (ch != traits_type::eof())
            {
                _target->push_back(static_cast<char>(ch));
                ++_writePos;
            }
            return ch;
        }

      private:
        std::string* _target;
        size_t _writePos = 0;
    };

    // Custom ostream that owns the streambuf.
    class MemoryOStream final: public std::ostream
    {
      public:
        MemoryOStream(std::string* target, bool append): std::ostream(&_buf), _buf(target, append) {}

      private:
        MemoryOutputBuf _buf;
    };

    // Custom streambuf backed by a string for reading.
    class MemoryInputBuf final: public std::streambuf
    {
      public:
        explicit MemoryInputBuf(std::string data): _data(std::move(data))
        {
            auto* begin = const_cast<char*>(_data.data());
            setg(begin, begin, begin + _data.size());
        }

      private:
        std::string _data;
    };

    class MemoryIStream final: public std::istream
    {
      public:
        explicit MemoryIStream(std::string const& data): std::istream(&_buf), _buf(data) {}

      private:
        MemoryInputBuf _buf;
    };

    // Combined read-write stream backed by in-memory data.
    class MemoryIOBuf final: public std::streambuf
    {
      public:
        explicit MemoryIOBuf(std::string* target): _target(target)
        {
            auto* begin = const_cast<char*>(_target->data());
            setg(begin, begin, begin + _target->size());
        }

      protected:
        std::streamsize xsputn(char const* s, std::streamsize n) override
        {
            _target->append(s, static_cast<size_t>(n));
            return n;
        }

        int_type overflow(int_type ch) override
        {
            if (ch != traits_type::eof())
                _target->push_back(static_cast<char>(ch));
            return ch;
        }

      private:
        std::string* _target;
    };

    class MemoryIOStream final: public std::iostream
    {
      public:
        explicit MemoryIOStream(std::string* target): std::iostream(&_buf), _buf(target) {}

      private:
        MemoryIOBuf _buf;
    };
} // namespace

InMemoryFileSystem::InMemoryFileSystem(std::initializer_list<FileEntry> entries)
{
    for (auto const& entry: entries)
    {
        auto perms = entry.perms;
        if (entry.isExecutable)
            perms |= std::filesystem::perms::owner_exec;

        if (entry.isDirectory)
        {
            addDirectory(entry.path);
        }
        else if (entry.isSymlink)
        {
            addSymlink(entry.path, std::filesystem::path(entry.content));
        }
        else
        {
            addFile(entry.path, entry.content, perms);
        }
    }
}

std::string InMemoryFileSystem::normalize(std::filesystem::path const& path) const
{
    auto result = path.is_relative() ? (_currentPath / path).lexically_normal().string()
                                     : path.lexically_normal().string();
    // Normalize to forward slashes for cross-platform consistency
    std::ranges::replace(result, '\\', '/');
    return result;
}

void InMemoryFileSystem::ensureParentDirectories(std::filesystem::path const& path) const
{
    auto p = std::filesystem::path(normalize(path)).parent_path();
    while (!p.empty() && p != p.root_path())
    {
        _directories.insert(p.string());
        p = p.parent_path();
    }
    if (!p.empty())
        _directories.insert(p.string());
}

bool InMemoryFileSystem::exists(std::filesystem::path const& path) const
{
    auto const key = normalize(path);
    return _files.contains(key) || _directories.contains(key) || _symlinks.contains(key);
}

bool InMemoryFileSystem::isDirectory(std::filesystem::path const& path) const
{
    return _directories.contains(normalize(path));
}

bool InMemoryFileSystem::isRegularFile(std::filesystem::path const& path) const
{
    return _files.contains(normalize(path));
}

bool InMemoryFileSystem::isSymlink(std::filesystem::path const& path) const
{
    return _symlinks.contains(normalize(path));
}

bool InMemoryFileSystem::isExecutableFile(std::filesystem::path const& path) const
{
    auto const key = normalize(path);
    if (!_files.contains(key) && !_symlinks.contains(key))
        return false;

    auto const it = _permissions.find(key);
    auto const perms = it != _permissions.end()
                           ? it->second
                           : std::filesystem::perms::owner_read | std::filesystem::perms::owner_write;
    return (perms & std::filesystem::perms::owner_exec) != std::filesystem::perms::none
           || (perms & std::filesystem::perms::group_exec) != std::filesystem::perms::none
           || (perms & std::filesystem::perms::others_exec) != std::filesystem::perms::none;
}

std::filesystem::path InMemoryFileSystem::weaklyCanonical(std::filesystem::path const& path) const
{
    return std::filesystem::path(normalize(path));
}

std::filesystem::path InMemoryFileSystem::currentPath() const
{
    return _currentPath;
}

std::expected<std::string, std::string> InMemoryFileSystem::readFile(std::filesystem::path const& path) const
{
    auto const key = normalize(path);
    if (_deniedPaths.contains(key))
        return std::unexpected(std::format("Permission denied: {}", key));
    if (auto const it = _files.find(key); it != _files.end())
        return it->second;
    return std::unexpected(std::format("File not found: {}", key));
}

std::expected<void, std::string> InMemoryFileSystem::writeFile(std::filesystem::path const& path,
                                                               std::string_view content) const
{
    auto const key = normalize(path);
    if (_deniedPaths.contains(key))
        return std::unexpected(std::format("Permission denied: {}", key));
    ensureParentDirectories(path);
    _files[key] = std::string(content);
    return {};
}

std::expected<void, std::string> InMemoryFileSystem::appendFile(std::filesystem::path const& path,
                                                                std::string_view content) const
{
    auto const key = normalize(path);
    if (_deniedPaths.contains(key))
        return std::unexpected(std::format("Permission denied: {}", key));
    ensureParentDirectories(path);
    _files[key].append(content);
    return {};
}

std::unique_ptr<std::istream> InMemoryFileSystem::openRead(std::filesystem::path const& path) const
{
    auto const key = normalize(path);
    if (_deniedPaths.contains(key))
        return nullptr;
    auto const it = _files.find(key);
    if (it == _files.end())
        return nullptr;
    return std::make_unique<MemoryIStream>(it->second);
}

std::unique_ptr<std::ostream> InMemoryFileSystem::openWrite(std::filesystem::path const& path,
                                                            bool append) const
{
    auto const key = normalize(path);
    if (_deniedPaths.contains(key))
        return nullptr;
    ensureParentDirectories(path);
    if (!_files.contains(key))
        _files[key] = {};
    return std::make_unique<MemoryOStream>(&_files[key], append);
}

std::unique_ptr<std::iostream> InMemoryFileSystem::openReadWrite(std::filesystem::path const& path) const
{
    auto const key = normalize(path);
    if (_deniedPaths.contains(key))
        return nullptr;
    ensureParentDirectories(path);
    if (!_files.contains(key))
        _files[key] = {};
    return std::make_unique<MemoryIOStream>(&_files[key]);
}

std::expected<void, std::string> InMemoryFileSystem::createDirectory(std::filesystem::path const& path) const
{
    auto const key = normalize(path);
    if (_deniedPaths.contains(key))
        return std::unexpected(std::format("Permission denied: {}", key));
    // Check that parent exists
    auto const parent = std::filesystem::path(key).parent_path().string();
    if (!parent.empty() && parent != "/" && !_directories.contains(parent))
        return std::unexpected(std::format("No such file or directory: {}", parent));
    _directories.insert(key);
    return {};
}

std::expected<void, std::string> InMemoryFileSystem::createDirectories(
    std::filesystem::path const& path) const
{
    auto p = std::filesystem::path(normalize(path));
    while (!p.empty() && p != p.root_path())
    {
        _directories.insert(p.string());
        p = p.parent_path();
    }
    if (!p.empty())
        _directories.insert(p.string());
    return {};
}

std::expected<bool, std::string> InMemoryFileSystem::remove(std::filesystem::path const& path) const
{
    auto const key = normalize(path);
    if (_deniedPaths.contains(key))
        return std::unexpected(std::format("Permission denied: {}", key));
    auto const fileErased = _files.erase(key) > 0;
    auto const dirErased = _directories.erase(key) > 0;
    auto const symlinkErased = _symlinks.erase(key) > 0;
    _permissions.erase(key);
    return fileErased || dirErased || symlinkErased;
}

std::expected<std::uintmax_t, std::string> InMemoryFileSystem::removeAll(
    std::filesystem::path const& path) const
{
    auto const prefix = normalize(path);
    if (_deniedPaths.contains(prefix))
        return std::unexpected(std::format("Permission denied: {}", prefix));
    std::uintmax_t count = 0;

    // Remove all files under this path
    for (auto it = _files.begin(); it != _files.end();)
    {
        if (it->first == prefix || it->first.starts_with(prefix + "/"))
        {
            it = _files.erase(it);
            ++count;
        }
        else
        {
            ++it;
        }
    }

    // Remove all directories under this path
    for (auto it = _directories.begin(); it != _directories.end();)
    {
        if (*it == prefix || it->starts_with(prefix + "/"))
        {
            it = _directories.erase(it);
            ++count;
        }
        else
        {
            ++it;
        }
    }

    // Remove all symlinks under this path
    for (auto it = _symlinks.begin(); it != _symlinks.end();)
    {
        if (it->first == prefix || it->first.starts_with(prefix + "/"))
        {
            it = _symlinks.erase(it);
            ++count;
        }
        else
        {
            ++it;
        }
    }

    return count;
}

std::expected<void, std::string> InMemoryFileSystem::copyFile(std::filesystem::path const& from,
                                                              std::filesystem::path const& to,
                                                              bool overwrite) const
{
    auto const srcKey = normalize(from);
    auto const dstKey = normalize(to);

    auto const it = _files.find(srcKey);
    if (it == _files.end())
        return std::unexpected(std::format("Source file not found: {}", srcKey));

    if (!overwrite && _files.contains(dstKey))
        return std::unexpected(std::format("Destination already exists: {}", dstKey));

    ensureParentDirectories(to);
    _files[dstKey] = it->second;
    return {};
}

std::expected<void, std::string> InMemoryFileSystem::rename(std::filesystem::path const& from,
                                                            std::filesystem::path const& to) const
{
    auto const srcKey = normalize(from);
    auto const dstKey = normalize(to);

    if (auto const it = _files.find(srcKey); it != _files.end())
    {
        ensureParentDirectories(to);
        _files[dstKey] = std::move(it->second);
        _files.erase(it);
        return {};
    }

    if (_directories.contains(srcKey))
    {
        auto const srcPrefix = srcKey.ends_with('/') ? srcKey : srcKey + "/";
        auto const dstPrefix = dstKey.ends_with('/') ? dstKey : dstKey + "/";

        // Move nested files
        auto filesToMove = std::vector<std::pair<std::string, std::string>> {};
        for (auto const& [filePath, content]: _files)
        {
            if (filePath.starts_with(srcPrefix))
                filesToMove.emplace_back(filePath, dstPrefix + filePath.substr(srcPrefix.size()));
        }
        for (auto const& [oldPath, newPath]: filesToMove)
        {
            _files[newPath] = std::move(_files[oldPath]);
            _files.erase(oldPath);
        }

        // Move nested directories
        auto dirsToMove = std::vector<std::pair<std::string, std::string>> {};
        for (auto const& dirPath: _directories)
        {
            if (dirPath.starts_with(srcPrefix))
                dirsToMove.emplace_back(dirPath, dstPrefix + dirPath.substr(srcPrefix.size()));
        }
        for (auto const& [oldPath, newPath]: dirsToMove)
        {
            _directories.erase(oldPath);
            _directories.insert(newPath);
        }

        // Move the directory itself
        _directories.erase(srcKey);
        _directories.insert(dstKey);
        ensureParentDirectories(to);
        return {};
    }

    return std::unexpected(std::format("Source not found: {}", srcKey));
}

std::expected<std::vector<FileSystem::DirectoryEntry>, std::string> InMemoryFileSystem::listDirectory(
    std::filesystem::path const& path) const
{
    auto const dirKey = normalize(path);
    if (!_directories.contains(dirKey))
        return std::unexpected(std::format("Not a directory: {}", dirKey));

    auto const prefix = dirKey.ends_with('/') ? dirKey : dirKey + "/";
    auto entries = std::vector<DirectoryEntry> {};

    // Collect direct children (files)
    for (auto const& [filePath, _]: _files)
    {
        if (!filePath.starts_with(prefix))
            continue;
        auto const rest = std::string_view(filePath).substr(prefix.size());
        if (rest.find('/') != std::string_view::npos)
            continue; // deeper than one level
        entries.push_back(DirectoryEntry {
            .path = std::filesystem::path(filePath),
            .isDirectory = false,
            .isRegularFile = true,
            .isSymlink = _symlinks.contains(filePath),
        });
    }

    // Collect direct children (directories)
    for (auto const& dirPath: _directories)
    {
        if (!dirPath.starts_with(prefix))
            continue;
        auto const rest = std::string_view(dirPath).substr(prefix.size());
        if (rest.empty() || rest.find('/') != std::string_view::npos)
            continue;
        entries.push_back(DirectoryEntry {
            .path = std::filesystem::path(dirPath),
            .isDirectory = true,
            .isRegularFile = false,
            .isSymlink = _symlinks.contains(dirPath),
        });
    }

    // Collect symlink-only entries (not already in _files or _directories)
    for (auto const& [symlinkPath, _]: _symlinks)
    {
        if (!symlinkPath.starts_with(prefix))
            continue;
        auto const rest = std::string_view(symlinkPath).substr(prefix.size());
        if (rest.find('/') != std::string_view::npos)
            continue;
        if (!_files.contains(symlinkPath) && !_directories.contains(symlinkPath))
        {
            entries.push_back(DirectoryEntry {
                .path = std::filesystem::path(symlinkPath),
                .isDirectory = false,
                .isRegularFile = false,
                .isSymlink = true,
            });
        }
    }

    return entries;
}

Generator<FileSystem::DirectoryEntry> InMemoryFileSystem::walkDirectoryRecursive(
    std::filesystem::path path, std::error_code* outError) const
{
    auto const dirKey = normalize(path);
    if (!_directories.contains(dirKey))
    {
        if (outError != nullptr)
            *outError = std::make_error_code(std::errc::not_a_directory);
        co_return; // Non-directory root yields nothing (callers pre-check, like NativeFileSystem).
    }

    auto const prefix = dirKey.ends_with('/') ? dirKey : dirKey + "/";

    // Depth below the root, counted from the normalized key: a direct child of the root
    // ("/root/a") has one path component after the prefix and depth 1, matching
    // NativeFileSystem's recursive_directory_iterator::depth() + 1 convention.
    auto const depthOf = [&prefix](std::string const& key) {
        return static_cast<int>(std::ranges::count(key.substr(prefix.size()), '/')) + 1;
    };

    // Gather every entry under the prefix, then yield in sorted path order so that
    // a directory always precedes its own contents ("a/b" < "a/b/c"), matching
    // NativeFileSystem's pre-order recursive_directory_iterator. (The maps are
    // sorted individually, but files-then-dirs-then-symlinks would otherwise break
    // the parents-before-children ordering that cp/rm rely on.)
    auto entries = std::vector<DirectoryEntry> {};
    for (auto const& [filePath, _]: _files)
        if (filePath.starts_with(prefix))
            entries.push_back({ .path = std::filesystem::path(filePath),
                                .isDirectory = false,
                                .isRegularFile = true,
                                .isSymlink = _symlinks.contains(filePath),
                                .depth = depthOf(filePath) });
    for (auto const& dirPath: _directories)
        if (dirPath.starts_with(prefix))
            entries.push_back({ .path = std::filesystem::path(dirPath),
                                .isDirectory = true,
                                .isRegularFile = false,
                                .isSymlink = _symlinks.contains(dirPath),
                                .depth = depthOf(dirPath) });
    for (auto const& [symlinkPath, _]: _symlinks)
        if (symlinkPath.starts_with(prefix) && !_files.contains(symlinkPath)
            && !_directories.contains(symlinkPath))
            entries.push_back({ .path = std::filesystem::path(symlinkPath),
                                .isDirectory = false,
                                .isRegularFile = false,
                                .isSymlink = true,
                                .depth = depthOf(symlinkPath) });

    std::ranges::sort(
        entries, std::ranges::less {}, [](DirectoryEntry const& e) { return e.path.generic_string(); });

    for (auto const& entry: entries)
        co_yield entry;
}

std::expected<std::uintmax_t, std::string> InMemoryFileSystem::fileSize(
    std::filesystem::path const& path) const
{
    auto const key = normalize(path);
    auto const it = _files.find(key);
    if (it == _files.end())
        return std::unexpected(std::format("File not found: {}", key));
    return static_cast<std::uintmax_t>(it->second.size());
}

std::expected<std::filesystem::file_time_type, std::string> InMemoryFileSystem::lastWriteTime(
    std::filesystem::path const& path) const
{
    auto const key = normalize(path);
    if (!_files.contains(key) && !_directories.contains(key))
        return std::unexpected(std::format("Path not found: {}", key));
    return std::filesystem::file_time_type::clock::now();
}

std::expected<std::filesystem::perms, std::string> InMemoryFileSystem::permissions(
    std::filesystem::path const& path) const
{
    auto const key = normalize(path);
    if (auto const it = _permissions.find(key); it != _permissions.end())
        return it->second;
    if (!exists(path))
        return std::unexpected(std::format("Path not found: {}", key));
    return std::filesystem::perms::owner_read | std::filesystem::perms::owner_write;
}

std::expected<void, std::string> InMemoryFileSystem::setPermissions(std::filesystem::path const& path,
                                                                    std::filesystem::perms perms) const
{
    auto const key = normalize(path);
    if (!exists(path))
        return std::unexpected(std::format("Path not found: {}", key));
    _permissions[key] = perms;
    return {};
}

std::expected<std::filesystem::path, std::string> InMemoryFileSystem::createTempFile(
    std::string_view prefix) const
{
    auto const name = std::format("/tmp/{}_{}", prefix, ++_tempCounter);
    _files[name] = {};
    ensureParentDirectories(std::filesystem::path(name));
    return std::filesystem::path(name);
}

void InMemoryFileSystem::setCurrentPath(std::filesystem::path const& path)
{
    _currentPath = path;
    _directories.insert(path.string());
}

void InMemoryFileSystem::addFile(std::filesystem::path const& path,
                                 std::string content,
                                 std::filesystem::perms perms)
{
    auto const key = normalize(path);
    _files[key] = std::move(content);
    _permissions[key] = perms;
    ensureParentDirectories(path);
}

void InMemoryFileSystem::addExecutable(std::filesystem::path const& path, std::string content)
{
    addFile(path,
            std::move(content),
            std::filesystem::perms::owner_read | std::filesystem::perms::owner_write
                | std::filesystem::perms::owner_exec);
}

void InMemoryFileSystem::addDirectory(std::filesystem::path const& path)
{
    auto p = std::filesystem::path(normalize(path));
    while (!p.empty() && p != p.root_path())
    {
        _directories.insert(p.string());
        p = p.parent_path();
    }
    if (!p.empty())
        _directories.insert(p.string());
}

void InMemoryFileSystem::addSymlink(std::filesystem::path const& path, std::filesystem::path const& target)
{
    auto const key = normalize(path);
    _symlinks[key] = target.string();
    ensureParentDirectories(path);
}

void InMemoryFileSystem::denyAccess(std::filesystem::path const& path)
{
    _deniedPaths.insert(normalize(path));
    _permissions[normalize(path)] = std::filesystem::perms::none;
}

} // namespace endo::platform::testing
