// SPDX-License-Identifier: Apache-2.0
#include "GlobMatcher.hpp"

#include <algorithm>
#include <filesystem>
#include <ranges>
#include <string>

#include <platform/PathUtils.hpp>

namespace endo
{

std::vector<std::string> expandGlobPattern(platform::FileSystem const& fileSystem, std::string_view pattern)
{
    namespace fs = std::filesystem;
    std::vector<std::string> results;
    std::string patternStr(pattern);

    auto const starstarPos = patternStr.find("**");
    if (starstarPos != std::string::npos)
    {
        return expandRecursiveGlob(fileSystem, patternStr);
    }

    fs::path patternPath(patternStr);

    fs::path dirPath = patternPath.parent_path();
    std::string filePattern = patternPath.filename().string();

    if (dirPath.empty())
        dirPath = ".";

    bool hasGlobChars = filePattern.find_first_of("*?[") != std::string::npos;

    if (!hasGlobChars)
    {
        return {};
    }

    auto const listing = fileSystem.listDirectory(dirPath);
    if (!listing)
    {
        return {};
    }

    for (auto const& entry: *listing)
    {
        std::string filename = entry.path.filename().string();
        if (globMatchFilename(filename, filePattern))
        {
            if (dirPath == ".")
                results.push_back(filename);
            else
                results.push_back(platform::normalizePath(entry.path));
        }
    }

    std::ranges::sort(results);

    return results;
}

namespace
{
    /// @brief Spells a walked path relative to the base the walk started from.
    ///
    /// walkDirectoryRecursive() yields whatever the filesystem considers the entry's path, and
    /// the two backends disagree about what that is for a base of ".": an injected filesystem
    /// resolves it to its own working directory and reports absolute paths, while the real one
    /// hands back "./sub/file". Re-anchoring the first and normalizing the second away leaves
    /// both spelled the way expandGlobPattern() spells a match in ".", i.e. with no "./"
    /// prefix, so `**` and `*` agree with each other and across backends.
    ///
    /// @param fileSystem The filesystem being walked; its working directory is the anchor.
    /// @param entryPath   The path as reported by the walk.
    /// @param basePath    The base the walk started from.
    /// @return The entry path relative to @p basePath when the base was ".", else normalized.
    [[nodiscard]] std::string relativizeToBase(platform::FileSystem const& fileSystem,
                                               std::filesystem::path const& entryPath,
                                               std::string_view basePath)
    {
        if (basePath != ".")
            return platform::normalizePath(entryPath);

        // The filesystem's working directory, never the process's: they are different views
        // whenever the filesystem is injected, which is the whole reason this exists.
        auto const relative = entryPath.lexically_relative(fileSystem.currentPath());
        if (!relative.empty())
            return platform::normalizePath(relative);

        // lexically_relative() gives up whenever the entry is relative and the working
        // directory absolute -- which is every entry the real filesystem yields for a walk
        // rooted at ".". Those already are relative to the base; only the leading "." the
        // iterator prepended has to go.
        return platform::normalizePath(entryPath.lexically_normal());
    }
} // namespace

std::vector<std::string> expandRecursiveGlob(platform::FileSystem const& fileSystem, std::string_view pattern)
{
    std::vector<std::string> results;
    std::string patternStr(pattern);

    auto const starstarPos = patternStr.find("**");
    if (starstarPos == std::string::npos)
        return {};

    std::string basePath = patternStr.substr(0, starstarPos);
    while (!basePath.empty() && (basePath.back() == '/' || basePath.back() == '\\'))
        basePath.pop_back();
    if (basePath.empty())
        basePath = ".";

    std::string suffixPattern = patternStr.substr(starstarPos + 2);
    while (!suffixPattern.empty() && (suffixPattern.front() == '/' || suffixPattern.front() == '\\'))
        suffixPattern.erase(0, 1);

    if (!fileSystem.isDirectory(basePath))
        return {};

    for (auto const& entry: fileSystem.walkDirectoryRecursive(basePath))
    {
        if (!entry.isRegularFile)
            continue;

        // Spelled the way expandGlobPattern() spells its results: relative to the walk root
        // when the pattern was relative. An injected filesystem resolves "." to an absolute
        // path, so without this the same pattern yields absolute paths in a test and
        // "./"-relative ones in the real shell.
        std::string filePath = relativizeToBase(fileSystem, entry.path, basePath);
        std::string filename = entry.path.filename().string();

        if (!suffixPattern.empty())
        {
            if (globMatchFilename(filename, suffixPattern))
                results.push_back(filePath);
        }
        else
        {
            results.push_back(filePath);
        }
    }

    std::ranges::sort(results);

    return results;
}

std::vector<size_t> findPrefixMatches(std::string_view text, std::string_view pattern)
{
    std::vector<size_t> matches;

    for (auto const len: std::views::iota(0uz, text.size() + 1))
    {
        if (globMatch(text.substr(0, len), pattern))
            matches.push_back(len);
    }

    return matches;
}

std::vector<size_t> findSuffixMatches(std::string_view text, std::string_view pattern)
{
    std::vector<size_t> matches;

    for (auto const start: std::views::iota(0uz, text.size() + 1))
    {
        if (globMatch(text.substr(start), pattern))
            matches.push_back(start);
    }

    return matches;
}

std::optional<size_t> findPatternMatchLength(std::string_view text, std::string_view pattern)
{
    for (auto const len: std::views::iota(1uz, text.size() + 1))
    {
        if (globMatch(text.substr(0, len), pattern))
            return len;
    }
    return std::nullopt;
}

} // namespace endo
