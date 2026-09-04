// SPDX-License-Identifier: Apache-2.0
#include "CommandResolver.hpp"

#include <endo-language/builtins/BuiltinSignatures.hpp>

#include <crispy/Utils.hpp>

#include <array>
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
    _cacheKey.clear();
    _pathCache.clear();
}

#if defined(_WIN32)
namespace
{
    /// Executable extensions used when %PATHEXT% is unset, empty, or all-blank. Lower-cased
    /// because Windows path comparison is case-insensitive and isExecutableFile() ignores case.
    constexpr std::array<std::string_view, 5> DefaultPathExt { ".exe", ".cmd", ".bat", ".com", ".ps1" };

    /// Trims leading/trailing ASCII whitespace from @p value.
    constexpr std::string_view trim(std::string_view value) noexcept
    {
        value = crispy::trimRight(value);
        while (!value.empty() && std::string_view(" \t\r\n").find(value.front()) != std::string_view::npos)
            value.remove_prefix(1);
        return value;
    }

    /// Returns the PATHEXT extension list, skipping empty/blank tokens (e.g. from a stray
    /// ";;") and falling back to DefaultPathExt when none remain — an empty token would
    /// otherwise re-introduce the bare-name probe and let an extensionless shim shadow the
    /// real executable.
    ///
    /// Entries are lower-cased. Windows ships PATHEXT upper-cased (".COM;.EXE;..."), and
    /// callers that *compare* an extension against this list need one canonical case;
    /// callers that build file names from it are unaffected, since the filesystem is
    /// case-insensitive.
    std::vector<std::string> pathExtList(EnvironmentProvider const& env)
    {
        auto extensions = std::vector<std::string> {};
        if (auto const pathext = env.get("PATHEXT"))
            for (auto const& raw: crispy::split(*pathext, ';'))
                if (auto const ext = trim(raw); !ext.empty())
                    extensions.emplace_back(crispy::toLower(std::string(ext)));

        if (extensions.empty())
            for (auto const& ext: DefaultPathExt)
                extensions.emplace_back(ext);

        return extensions;
    }
} // namespace
#endif

std::vector<std::string> CommandResolver::executableExtensions(
    [[maybe_unused]] EnvironmentProvider const& env)
{
#if defined(_WIN32)
    return pathExtList(env);
#else
    return {};
#endif
}

std::vector<std::string> CommandResolver::candidateNames([[maybe_unused]] EnvironmentProvider const& env,
                                                         std::string_view command)
{
#if !defined(_WIN32)
    return { std::string(command) };
#else
    // cmd.exe / PowerShell semantics: a name typed *with* an extension is used verbatim and
    // PATHEXT is not applied; a bare name is expanded only to "command + ext" for each
    // PATHEXT entry, and the extensionless name itself is never probed. The latter is what
    // stops an extensionless "docker" shim from shadowing the real "docker.exe".
    if (std::filesystem::path(command).has_extension())
        return { std::string(command) };

    auto names = std::vector<std::string> {};
    for (auto const& ext: pathExtList(env))
        names.emplace_back(std::string(command) + ext);
    return names;
#endif
}

std::string CommandResolver::findInPath(std::string_view command) const
{
    auto const matches = search(command, /*firstOnly=*/true);
    return matches.empty() ? std::string {} : matches.front();
}

std::vector<std::string> CommandResolver::findAllInPath(std::string_view command) const
{
    return search(command, /*firstOnly=*/false);
}

std::vector<std::string> CommandResolver::search(std::string_view command, bool firstOnly) const
{
    // Candidate file names are independent of the PATH directory, so compute them once.
    auto const names = candidateNames(_env, command);

    auto results = std::vector<std::string> {};

    for (auto const& dir: pathDirectories(_env))
    {
        if (!_fs.exists(dir) || !_fs.isDirectory(dir))
            continue;

        for (auto const& name: names)
        {
            auto const candidate = dir / name;
            if (!_fs.isExecutableFile(candidate))
                continue;
#if defined(_WIN32)
            results.push_back(platform::normalizePath(candidate));
#else
            results.push_back(candidate.string());
#endif
            if (firstOnly)
                return results;
            break; // At most one match per PATH directory
        }
    }

    return results;
}

void CommandResolver::refreshCacheIfNeeded() const
{
    auto const currentKey = resolutionCacheKey(_env);

    if (currentKey != _cacheKey)
    {
        _cacheKey = currentKey;
        _pathCache.clear();
    }
}

std::string CommandResolver::resolutionCacheKey(EnvironmentProvider const& env)
{
    auto key = env.get("PATH").value_or(std::string {});
#if defined(_WIN32)
    // PATHEXT also governs resolution on Windows, so a change to it must invalidate the
    // cache too. '\x1f' (unit separator) cannot occur in a PATH/PATHEXT value.
    key.append("\x1f").append(env.get("PATHEXT").value_or(std::string {}));
#endif
    return key;
}

std::vector<std::filesystem::path> CommandResolver::pathDirectories(EnvironmentProvider const& env)
{
    auto const pathEnv = env.get("PATH");
    if (!pathEnv)
        return {};

#if defined(_WIN32)
    auto const pathSep = ';';
#else
    auto const pathSep = ':';
#endif

    auto directories = std::vector<std::filesystem::path> {};
    for (auto const& pathStr: crispy::split(*pathEnv, pathSep))
        if (!pathStr.empty())
            directories.emplace_back(pathStr);
    return directories;
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
