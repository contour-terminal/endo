// SPDX-License-Identifier: Apache-2.0
#include <platform/testing/InMemoryFileSystem.hpp>

#include <algorithm>
#include <format>
#include <sstream>

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
    if (path.is_relative())
        return (_currentPath / path).lexically_normal().string();
    return path.lexically_normal().string();
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

std::filesystem::path InMemoryFileSystem::weaklyCanonical(std::filesystem::path const& path) const
{
    return std::filesystem::path(normalize(path));
}

std::filesystem::path InMemoryFileSystem::currentPath() const
{
    return _currentPath;
}

std::expected<std::string, std::string> InMemoryFileSystem::readFile(
    std::filesystem::path const& path) const
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
    ensureParentDirectories(path);
    _files[key].append(content);
    return {};
}

std::unique_ptr<std::istream> InMemoryFileSystem::openRead(std::filesystem::path const& path) const
{
    auto const key = normalize(path);
    auto const it = _files.find(key);
    if (it == _files.end())
        return nullptr;
    return std::make_unique<MemoryIStream>(it->second);
}

std::unique_ptr<std::ostream> InMemoryFileSystem::openWrite(std::filesystem::path const& path,
                                                             bool append) const
{
    auto const key = normalize(path);
    ensureParentDirectories(path);
    if (!_files.contains(key))
        _files[key] = {};
    return std::make_unique<MemoryOStream>(&_files[key], append);
}

std::unique_ptr<std::iostream> InMemoryFileSystem::openReadWrite(
    std::filesystem::path const& path) const
{
    auto const key = normalize(path);
    ensureParentDirectories(path);
    if (!_files.contains(key))
        _files[key] = {};
    return std::make_unique<MemoryIOStream>(&_files[key]);
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
            ++it;
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
            ++it;
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
            ++it;
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
        _directories.erase(srcKey);
        _directories.insert(dstKey);
        return {};
    }

    return std::unexpected(std::format("Source not found: {}", srcKey));
}

std::expected<std::vector<FileSystem::DirectoryEntry>, std::string>
InMemoryFileSystem::listDirectory(std::filesystem::path const& path) const
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

    return entries;
}

std::expected<std::vector<FileSystem::DirectoryEntry>, std::string>
InMemoryFileSystem::listDirectoryRecursive(std::filesystem::path const& path) const
{
    auto const dirKey = normalize(path);
    if (!_directories.contains(dirKey))
        return std::unexpected(std::format("Not a directory: {}", dirKey));

    auto const prefix = dirKey.ends_with('/') ? dirKey : dirKey + "/";
    auto entries = std::vector<DirectoryEntry> {};

    for (auto const& [filePath, _]: _files)
    {
        if (filePath.starts_with(prefix))
        {
            entries.push_back(DirectoryEntry {
                .path = std::filesystem::path(filePath),
                .isDirectory = false,
                .isRegularFile = true,
                .isSymlink = _symlinks.contains(filePath),
            });
        }
    }

    for (auto const& dirPath: _directories)
    {
        if (dirPath.starts_with(prefix))
        {
            entries.push_back(DirectoryEntry {
                .path = std::filesystem::path(dirPath),
                .isDirectory = true,
                .isRegularFile = false,
                .isSymlink = _symlinks.contains(dirPath),
            });
        }
    }

    return entries;
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

std::expected<void, std::string> InMemoryFileSystem::setPermissions(
    std::filesystem::path const& path, std::filesystem::perms perms) const
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

void InMemoryFileSystem::addFile(std::filesystem::path const& path, std::string content,
                                  std::filesystem::perms perms)
{
    auto const key = normalize(path);
    _files[key] = std::move(content);
    _permissions[key] = perms;
    ensureParentDirectories(path);
}

void InMemoryFileSystem::addExecutable(std::filesystem::path const& path, std::string content)
{
    addFile(path, std::move(content),
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

void InMemoryFileSystem::addSymlink(std::filesystem::path const& path,
                                     std::filesystem::path const& target)
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
