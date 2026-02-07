// SPDX-License-Identifier: Apache-2.0
#include "CommandResolver.hpp"
#include <shell/Environment.hpp>

#include <crispy/utils.h>

#include <filesystem>

namespace endo
{

CommandResolver::CommandResolver(Environment const& env): _env(env)
{
}

CommandInfo CommandResolver::resolve(std::string_view command) const
{
    if (command.empty())
        return { CommandType::NotFound, "command not found" };

    // Check for builtins first
    auto const& builtins = builtinNames();
    if (builtins.count(std::string(command)))
        return { CommandType::Builtin, "shell builtin" };

    // TODO: Check for aliases when implemented
    // For now, aliases are a placeholder

    // Search PATH for executable
    refreshCacheIfNeeded();

    auto const cmdStr = std::string(command);
    auto it = _pathCache.find(cmdStr);
    if (it != _pathCache.end())
    {
        if (it->second.empty())
            return { CommandType::NotFound, "command not found" };
        return { CommandType::External, it->second };
    }

    // Not in cache, search PATH
    auto const path = findInPath(command);
    _pathCache[cmdStr] = path;

    if (path.empty())
        return { CommandType::NotFound, "command not found" };
    return { CommandType::External, path };
}

void CommandResolver::invalidateCache()
{
    _cachedPath.clear();
    _pathCache.clear();
}

std::string CommandResolver::findInPath(std::string_view command) const
{
    auto const pathEnv = _env.get("PATH");
    if (!pathEnv)
        return {};

    auto const paths = crispy::split(*pathEnv, ':');

    for (auto const& pathStr: paths)
    {
        if (pathStr.empty())
            continue;

        std::error_code ec;
        std::filesystem::path dir(pathStr);

        if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec))
            continue;

        auto const candidate = dir / std::string(command);

        if (!std::filesystem::exists(candidate, ec))
            continue;

        if (!std::filesystem::is_regular_file(candidate, ec) && !std::filesystem::is_symlink(candidate, ec))
            continue;

        // Check if executable
        auto status = std::filesystem::status(candidate, ec);
        if (ec)
            continue;

        auto perms = status.permissions();
        bool isExecutable = (perms & std::filesystem::perms::owner_exec) != std::filesystem::perms::none
                            || (perms & std::filesystem::perms::group_exec) != std::filesystem::perms::none
                            || (perms & std::filesystem::perms::others_exec) != std::filesystem::perms::none;

        if (isExecutable)
            return candidate.string();
    }

    return {};
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
    static std::set<std::string> const names = {
        // Shell builtins
        "cat",
        "cd",
        "exit",
        "export",
        "set",
        "unset",
        "read",
        "sleep",
        "true",
        "false",
        "jobs",
        "fg",
        "bg",
        "wait",
        "bind",
        "which",
        // Control flow keywords
        "if",
        "then",
        "else",
        "elif",
        "fi",
        "for",
        "while",
        "do",
        "done",
        "case",
        "esac",
        "in",
        "function",
        "return",
        "break",
        "continue",
    };
    return names;
}

} // namespace endo
