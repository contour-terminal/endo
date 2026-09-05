// SPDX-License-Identifier: Apache-2.0
#include "PathCommandIndex.hpp"
#include <shell/util/CommandResolver.hpp>

#include <crispy/Utils.hpp>

#include <algorithm>
#include <map>
#include <optional>

#include <platform/PathUtils.hpp>

namespace endo
{

namespace
{
    /// @brief Returns the command name a $PATH entry would be invoked by, if it is executable.
    ///
    /// This is the forward direction of CommandResolver::candidateNames(): that maps a typed
    /// name to the files worth probing, this maps a file back to the name that reaches it.
    ///
    /// @param fs         Filesystem used to test executability.
    /// @param entry      The directory entry to classify.
    /// @param extensions Executable extensions from CommandResolver::executableExtensions();
    ///                   empty on POSIX, where the permission bits decide instead.
    /// @return The command name, or nullopt when @p entry cannot be run.
    std::optional<std::string> commandNameFor(FileSystem const& fs,
                                              FileSystem::DirectoryEntry const& entry,
                                              [[maybe_unused]] std::vector<std::string> const& extensions)
    {
        // Not redundant with isExecutableFile(): a symlink to a directory passes that test.
        if (entry.isDirectory)
            return std::nullopt;

#if defined(_WIN32)
        // Windows executability is by extension, and the command is typed without it.
        auto const extension = crispy::toLower(entry.path.extension().string());
        if (std::ranges::find(extensions, extension) == extensions.end())
            return std::nullopt;
        auto name = entry.path.stem().string();
#else
        auto name = entry.path.filename().string();
#endif
        if (name.empty() || !fs.isExecutableFile(entry.path))
            return std::nullopt;
        return name;
    }
} // namespace

PathCommandIndex::PathCommandIndex(EnvironmentProvider const& env, FileSystem const& fs): _env(env), _fs(fs)
{
}

std::vector<std::pair<std::string, std::string>> const& PathCommandIndex::entries() const
{
    // An unset or empty $PATH scans to nothing, which is exactly the initial state, so the
    // starting empty cache key needs no separate "not yet populated" flag.
    if (auto currentKey = CommandResolver::resolutionCacheKey(_env); currentKey != _cacheKey)
    {
        _cacheKey = std::move(currentKey);
        _entries = scan();
    }
    return _entries;
}

std::vector<std::pair<std::string, std::string>> PathCommandIndex::scan() const
{
    // Sorted, and first occurrence wins: the reported path is the one that would run.
    auto commands = std::map<std::string, std::string> {};

    auto const extensions = CommandResolver::executableExtensions(_env);

    for (auto const& dir: CommandResolver::pathDirectories(_env))
    {
        // listDirectory() already fails for a missing or non-directory path, so no
        // separate exists()/isDirectory() probe is needed.
        auto const listing = _fs.listDirectory(dir);
        if (!listing)
            continue;

        for (auto const& entry: *listing)
            if (auto name = commandNameFor(_fs, entry, extensions))
                commands.try_emplace(*std::move(name), platform::normalizePath(entry.path));
    }

    return { commands.begin(), commands.end() };
}

} // namespace endo
