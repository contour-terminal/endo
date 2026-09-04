// SPDX-License-Identifier: Apache-2.0
#include "PathCommandIndex.hpp"
#include <shell/util/CommandResolver.hpp>

#include <crispy/Utils.hpp>

#include <algorithm>
#include <filesystem>
#include <map>

#include <platform/PathUtils.hpp>

namespace endo
{

namespace
{
    /// The separator between $PATH entries: ';' on Windows, ':' elsewhere.
    constexpr char PathSeparator =
#if defined(_WIN32)
        ';';
#else
        ':';
#endif

    /// Separates $PATH from $PATHEXT in the cache key. A unit separator cannot occur in
    /// either value, so no combination of them can collide with a different pair.
    constexpr char CacheKeySeparator = '\x1f';

} // namespace

std::string collapseHomePrefix(std::string path, std::string_view home)
{
    if (home.empty() || !path.starts_with(home))
        return path;

    path.replace(0, home.size(), "~");
    return path;
}

std::string homeDirectory(EnvironmentProvider const& env)
{
    return env.get("HOME").value_or(std::string {});
}

PathCommandIndex::PathCommandIndex(EnvironmentProvider const& env, FileSystem const& fs): _env(env), _fs(fs)
{
}

std::string PathCommandIndex::computeCacheKey() const
{
    auto key = std::string(_env.get("PATH").value_or(""));
#if defined(_WIN32)
    key += CacheKeySeparator;
    key += _env.get("PATHEXT").value_or("");
#endif
    return key;
}

std::vector<std::pair<std::string, std::string>> const& PathCommandIndex::entries() const
{
    // An unset or empty $PATH scans to nothing, which is exactly the initial state, so the
    // starting empty cache key needs no separate "not yet populated" flag.
    if (auto currentKey = computeCacheKey(); currentKey != _cacheKey)
    {
        _cacheKey = std::move(currentKey);
        _entries = scan();
    }
    return _entries;
}

void PathCommandIndex::invalidateCache()
{
    _entries.clear();
    _cacheKey.clear();
}

std::vector<std::pair<std::string, std::string>> PathCommandIndex::scan() const
{
    auto const pathEnv = _env.get("PATH");
    if (!pathEnv)
        return {};

    // Sorted, and first occurrence wins: the reported path is the one that would run.
    auto commands = std::map<std::string, std::string> {};

#if defined(_WIN32)
    // Shared with CommandResolver so completion and resolution agree on what can run.
    // POSIX needs no equivalent: there the permission bits decide, not the name.
    auto const extensions = CommandResolver::executableExtensions(_env);
#endif

    for (auto const& pathStr: crispy::split(*pathEnv, PathSeparator))
    {
        if (pathStr.empty())
            continue;

        auto const dir = std::filesystem::path(pathStr);
        if (!_fs.exists(dir) || !_fs.isDirectory(dir))
            continue;

        auto const listing = _fs.listDirectory(dir);
        if (!listing)
            continue;

        for (auto const& entry: *listing)
        {
            if (entry.isDirectory)
                continue;

#if defined(_WIN32)
            // Windows executability is by extension; only %PATHEXT% names can run, and the
            // command is typed without the extension, so key on the stem.
            auto const extension = crispy::toLower(entry.path.extension().string());
            if (std::ranges::find(extensions, extension) == extensions.end())
                continue;
            auto name = entry.path.stem().string();
#else
            auto name = entry.path.filename().string();
#endif
            if (name.empty() || !_fs.isExecutableFile(entry.path))
                continue;

            commands.try_emplace(std::move(name), platform::normalizePath(entry.path));
        }
    }

    return { commands.begin(), commands.end() };
}

} // namespace endo
