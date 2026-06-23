// SPDX-License-Identifier: Apache-2.0
#include "CommandResolver.hpp"

#include <endo-language/builtins/BuiltinSignatures.hpp>

#include <crispy/utils.h>

#include <filesystem>

#include <platform/EnvironmentProvider.hpp>
#include <platform/PathUtils.hpp>

namespace endo
{

CommandResolver::CommandResolver(EnvironmentProvider const& env, FileSystem const& fs): _env(env), _fs(fs)
{
}

CommandInfo CommandResolver::resolve(std::string_view command) const
{
    if (command.empty())
        return { .type = CommandType::NotFound, .tooltip = "command not found" };

    // Check for builtins first
    auto const& builtins = builtinNames();
    if (builtins.count(std::string(command)))
        return { .type = CommandType::Builtin, .tooltip = "shell builtin" };

    // TODO: Check for aliases when implemented
    // For now, aliases are a placeholder

    // Search PATH for executable
    refreshCacheIfNeeded();

    auto const cmdStr = std::string(command);
    auto it = _pathCache.find(cmdStr);
    if (it != _pathCache.end())
    {
        if (it->second.empty())
            return { .type = CommandType::NotFound, .tooltip = "command not found" };
        return { .type = CommandType::External, .tooltip = it->second };
    }

    // Not in cache, search PATH
    auto const path = findInPath(command);
    _pathCache[cmdStr] = path;

    if (path.empty())
        return { .type = CommandType::NotFound, .tooltip = "command not found" };
    return { .type = CommandType::External, .tooltip = path };
}

void CommandResolver::invalidateCache()
{
    _cachedPath.clear();
    _pathCache.clear();
}

std::string CommandResolver::findInPath(std::string_view command) const
{
    auto const matches = findAllInPath(command);
    return matches.empty() ? std::string {} : matches.front();
}

std::vector<std::string> CommandResolver::findAllInPath(std::string_view command) const
{
    auto const pathEnv = _env.get("PATH");
    if (!pathEnv)
        return {};

#if defined(_WIN32)
    auto const pathSep = ';';
#else
    auto const pathSep = ':';
#endif

    auto const paths = crispy::split(*pathEnv, pathSep);

#if defined(_WIN32)
    // Build PATHEXT extensions list
    auto extensions = std::vector<std::string> {};
    if (auto const pathext = _env.get("PATHEXT"))
    {
        auto const exts = crispy::split(*pathext, ';');
        for (auto const& ext: exts)
            extensions.emplace_back(ext);
    }
    else
    {
        extensions = { ".exe", ".cmd", ".bat", ".com", ".ps1" };
    }
#endif

    auto results = std::vector<std::string> {};

    for (auto const& pathStr: paths)
    {
        if (pathStr.empty())
            continue;

        std::filesystem::path dir(pathStr);

        if (!_fs.exists(dir) || !_fs.isDirectory(dir))
            continue;

        auto const candidate = dir / std::string(command);

#if defined(_WIN32)
        // On Windows, check the exact name first, then try each PATHEXT extension.
        auto const candidates = [&]() {
            auto result = std::vector<std::filesystem::path> {};
            result.push_back(candidate);
            for (auto const& ext: extensions)
                result.push_back(std::filesystem::path(candidate.string() + ext));
            return result;
        }();

        for (auto const& cand: candidates)
        {
            if (!_fs.isExecutableFile(cand))
                continue;
            results.push_back(platform::normalizePath(cand));
            break; // At most one match per PATH directory
        }
#else
        if (_fs.isExecutableFile(candidate))
            results.push_back(candidate.string());
#endif
    }

    return results;
}

void CommandResolver::refreshCacheIfNeeded() const
{
    auto const pathEnv = _env.get("PATH");
    std::string currentPath = pathEnv ? std::string(*pathEnv) : "";

    if (currentPath != _cachedPath)
    {
        _cachedPath = currentPath;
        _pathCache.clear();
    }
}

std::set<std::string> const& CommandResolver::builtinNames()
{
    static auto const names = [] {
        auto const builtins = userFacingBuiltins();
        auto result = std::set<std::string>();
        for (auto const& info: builtins)
            result.insert(info.name);
        return result;
    }();
    return names;
}

} // namespace endo
