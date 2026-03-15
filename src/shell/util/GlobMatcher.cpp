// SPDX-License-Identifier: Apache-2.0
#include "GlobMatcher.hpp"

#include <algorithm>
#include <filesystem>
#include <ranges>
#include <string>

namespace endo
{

std::vector<std::string> expandGlobPattern(std::string_view pattern)
{
    namespace fs = std::filesystem;
    std::vector<std::string> results;
    std::string patternStr(pattern);

    auto const starstarPos = patternStr.find("**");
    if (starstarPos != std::string::npos)
    {
        return expandRecursiveGlob(patternStr);
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

    std::error_code ec;
    if (!fs::exists(dirPath, ec) || ec)
    {
        return {};
    }

    for (auto const& entry: fs::directory_iterator(dirPath, ec))
    {
        if (ec)
            break;

        std::string filename = entry.path().filename().string();
        if (globMatchFilename(filename, filePattern))
        {
            if (dirPath == ".")
                results.push_back(filename);
            else
                results.push_back(entry.path().string());
        }
    }

    std::ranges::sort(results);

    return results;
}

std::vector<std::string> expandRecursiveGlob(std::string_view pattern)
{
    namespace fs = std::filesystem;
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

    std::error_code ec;
    if (!fs::exists(basePath, ec) || ec)
        return {};

    for (auto const& entry: fs::recursive_directory_iterator(basePath, ec))
    {
        if (ec)
            break;

        if (!entry.is_regular_file())
            continue;

        std::string filePath = entry.path().string();
        std::string filename = entry.path().filename().string();

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
