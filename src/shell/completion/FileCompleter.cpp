// SPDX-License-Identifier: Apache-2.0
#include "FileCompleter.hpp"

#include <endo-language/ide/CompletionCandidates.hpp>

#include <tui/completer/FuzzyMatch.hpp>
#include <tui/completer/SmartCaseMatch.hpp>

#include <algorithm>
#include <cstdlib>

#include <platform/PathUtils.hpp>

#if !defined(_WIN32)
    #include <pwd.h>
    #include <unistd.h>
#endif

namespace endo
{

std::vector<CompletionItem> FileCompleter::complete(CompletionContext const& context)
{
    // Skip file suggestions for builtins that accept only enumerated values
    if (context.command.has_value() && isBuiltinWithArgumentCompletion(*context.command))
        return {};

    // Normalize backslashes to forward slashes on Windows so that user-typed
    // backslash paths are handled correctly in all slash-related logic below.
    auto const normalizedPrefix = platform::normalizePath(std::string(context.prefix));
    std::string_view const prefix = normalizedPrefix;

    if (prefix.empty())
    {
        // Complete in current directory
        return listDirectory(".", "", "");
    }

    // Expand tilde if present
    std::filesystem::path expandedPath = expandTilde(prefix);
    std::string pathPrefix;

    // Determine if prefix had tilde that we need to preserve
    if (!prefix.empty() && prefix[0] == '~')
    {
        // Find where the tilde expansion ends
        auto tildeEnd = prefix.find('/');
        if (tildeEnd == std::string_view::npos)
        {
            // Just "~" or "~user" without trailing path
            pathPrefix = std::string(prefix);
        }
        else
        {
            pathPrefix = std::string(prefix.substr(0, tildeEnd));
        }
    }

    std::error_code ec;
    std::filesystem::path dir;
    std::string filePrefix;

    if (std::filesystem::is_directory(expandedPath, ec) && !ec)
    {
        // If path is a directory and ends with /, list its contents
        if (!prefix.empty() && prefix.back() == '/')
        {
            dir = expandedPath;
            filePrefix = "";
            pathPrefix = std::string(prefix);
        }
        else
        {
            // Path is a directory but doesn't end with /, treat as complete match
            // and offer trailing slash
            std::vector<CompletionItem> results;
            std::string completedPath = std::string(prefix) + "/";
            results.push_back(
                CompletionItem { .text = completedPath,
                                 .displayText = std::filesystem::path(prefix).filename().string() + "/",
                                 .description = "directory",
                                 .score = 100 });
            return results;
        }
    }
    else
    {
        // Path doesn't exist as-is, split into directory and prefix
        dir = expandedPath.parent_path();
        filePrefix = expandedPath.filename().string();

        // Build the path prefix for display
        if (dir.empty())
        {
            dir = ".";
            pathPrefix = "";
        }
        else
        {
            // Preserve the original path format (with tilde if present)
            if (!prefix.empty() && prefix[0] == '~')
            {
                auto lastSlash = prefix.rfind('/');
                if (lastSlash != std::string_view::npos)
                    pathPrefix = std::string(prefix.substr(0, lastSlash + 1));
                else
                    pathPrefix = "";
            }
            else
            {
                pathPrefix = platform::normalizePath(dir);
                if (!pathPrefix.empty() && pathPrefix.back() != '/')
                    pathPrefix += '/';
            }
        }
    }

    // Check if directory exists
    if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec))
        return {};

    return listDirectory(dir, filePrefix, pathPrefix);
}

bool FileCompleter::canHandle(CompletionContextType type) const
{
    return type == CompletionContextType::FilePath || type == CompletionContextType::Argument
           || type == CompletionContextType::Redirect;
}

std::filesystem::path FileCompleter::expandTilde(std::string_view path)
{
    if (path.empty() || path[0] != '~')
        return std::filesystem::path(path);

    std::string result;

    if (path.size() == 1 || path[1] == '/')
    {
        // ~ or ~/... - current user's home
        if (char const* home = std::getenv("HOME"))
        {
            result = home;
#if defined(_WIN32)
            else if (char const* userProfile = std::getenv("USERPROFILE")) result = userProfile;
#else
        }
        else if (struct passwd* pw = getpwuid(getuid()))
        {
            result = pw->pw_dir;
#endif
        }
        else
        {
        }

        if (path.size() > 1)
            result += std::string(path.substr(1));
    }
    else
    {
        // ~user/... - specific user's home
        auto slashPos = path.find('/');
        std::string username(
            path.substr(1, slashPos == std::string_view::npos ? std::string_view::npos : slashPos - 1));

#if !defined(_WIN32)
        if (struct passwd* pw = getpwnam(username.c_str()))
        {
            result = pw->pw_dir;
            if (slashPos != std::string_view::npos)
                result += std::string(path.substr(slashPos));
        }
        else
#endif
        {
            // User not found, return path as-is
            return std::filesystem::path(path);
        }
    }

    return std::filesystem::path(result);
}

std::string FileCompleter::escapeForShell(std::string_view path)
{
    std::string result;
    result.reserve(path.size());

    for (char ch: path)
    {
        switch (ch)
        {
            case ' ':
            case '\\':
            case '\'':
            case '"':
            case '`':
            case '$':
            case '!':
            case '&':
            case '|':
            case ';':
            case '(':
            case ')':
            case '<':
            case '>':
            case '*':
            case '?':
            case '[':
            case ']':
            case '#':
            case '~':
                result += '\\';
                result += ch;
                break;
            default: result += ch; break;
        }
    }

    return result;
}

bool FileCompleter::isHidden(std::string_view name)
{
    return !name.empty() && name[0] == '.';
}

std::vector<CompletionItem> FileCompleter::listDirectory(std::filesystem::path const& dir,
                                                         std::string_view prefix,
                                                         std::string_view pathPrefix)
{
    std::vector<CompletionItem> results;
    std::error_code ec;

    // Determine if we should show hidden files
    bool showHidden = !prefix.empty() && prefix[0] == '.';

    // Configuration for fuzzy matching
    tui::FuzzyConfig fuzzyConfig;
    double const minThreshold = fuzzyConfig.minMatchThreshold;

    for (auto const& entry: std::filesystem::directory_iterator(dir, ec))
    {
        if (ec)
            break;

        auto filename = entry.path().filename().string();

        // Skip hidden files unless prefix starts with dot
        if (isHidden(filename) && !showHidden)
            continue;

        // Option C: Check both prefix and fuzzy matches
        bool isPrefixMatch = tui::SmartCaseMatch::matchesPrefix(filename, prefix);
        tui::FuzzyMatchResult fuzzyResult;
        bool isFuzzyMatch = false;

        if (!isPrefixMatch && !prefix.empty())
        {
            // Try fuzzy matching only if not a prefix match
            fuzzyResult = tui::FuzzyMatch::matchSmartCase(filename, prefix);
            size_t textLen = tui::FuzzyMatch::countGraphemes(filename);
            isFuzzyMatch =
                fuzzyResult.matches
                && (fuzzyResult.quality(textLen) >= minThreshold || fuzzyResult.isContiguousSubstring());
        }

        // Skip if neither prefix nor fuzzy match
        if (!isPrefixMatch && !isFuzzyMatch)
            continue;

        bool isDir = entry.is_directory(ec);
        std::string displayName = filename;
        std::string fullPath = std::string(pathPrefix) + filename;

        if (isDir)
        {
            displayName += "/";
            fullPath += "/";
        }

        int baseScore = isDir ? 80 : 50; // Directories get slightly higher priority
        int score = 0;
        std::vector<size_t> matchPositions;

        if (isPrefixMatch)
        {
            // Prefix matches use SmartCaseMatch scoring with bonus
            score = tui::SmartCaseMatch::adjustScore(baseScore, filename, prefix);
            score += fuzzyConfig.prefixMatchBonus; // Prefix gets priority over fuzzy
            // No matchPositions for prefix matches (no special highlighting needed)
        }
        else
        {
            // Fuzzy matches use FuzzyMatch scoring
            score = tui::FuzzyMatch::calculateScore(baseScore, filename, prefix, fuzzyResult, fuzzyConfig);
            matchPositions = std::move(fuzzyResult.positions);
        }

        results.push_back(CompletionItem { .text = fullPath,
                                           .displayText = displayName,
                                           .description = isDir ? "directory" : "",
                                           .score = score,
                                           .matchPositions = std::move(matchPositions) });
    }

    // Sort: directories first, then by score (descending), then alphabetically
    std::ranges::sort(results, [](auto const& a, auto const& b) {
        bool aIsDir = !a.displayText.empty() && a.displayText.back() == '/';
        bool bIsDir = !b.displayText.empty() && b.displayText.back() == '/';

        if (aIsDir != bIsDir)
            return aIsDir > bIsDir;

        // Higher score first
        if (a.score != b.score)
            return a.score > b.score;

        return a.displayText < b.displayText;
    });

    return results;
}

} // namespace endo
