// SPDX-License-Identifier: Apache-2.0
#include "FileCompleter.hpp"

#include <endo-language/ide/CompletionCandidates.hpp>

#include <tui/completer/FuzzyMatch.hpp>
#include <tui/completer/SmartCaseMatch.hpp>

#include <algorithm>

#include <platform/PathUtils.hpp>

#if !defined(_WIN32)
    #include <pwd.h>
#endif

namespace endo
{

FileCompleter::FileCompleter(EnvironmentProvider const& env, FileSystem const& fs): _env(env), _fs(fs)
{
}

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

    // Returns the on-disk capitalization of an existing absolute path so completions
    // echo the real case rather than the case the user typed (e.g. "D:/foo" -> "D:/Foo").
    // This deliberately bypasses _fs: FileSystem models no case-canonicalization, and
    // canonicalCasePath() is a Windows-only on-disk query (identity on POSIX). An injected
    // filesystem addressed by a drive-letter path would therefore be asked about the real
    // drive's casing -- add a FileSystem::canonicalCase() before writing such a test.
    // Relative and tilde-prefixed paths are left exactly as typed: canonicalCasePath()
    // resolves to an absolute path and would otherwise rewrite "foo/" into an absolute
    // path or expand "~/foo" into the literal home directory.
    auto const caseCorrect = [](std::filesystem::path const& path, std::string_view typed) -> std::string {
        bool const isTilde = !typed.empty() && typed.front() == '~';
        return (path.is_absolute() && !isTilde) ? platform::canonicalCasePath(path) : std::string(typed);
    };

    // Appends a trailing '/' to a non-empty directory prefix so listed children are
    // joined onto it correctly.
    auto const ensureTrailingSlash = [](std::string& s) {
        if (!s.empty() && s.back() != '/')
            s += '/';
    };

    std::filesystem::path dir;
    std::string filePrefix;

    if (_fs.isDirectory(expandedPath))
    {
        // If path is a directory and ends with /, list its contents
        if (!prefix.empty() && prefix.back() == '/')
        {
            dir = expandedPath;
            filePrefix = "";
            // Echo the directory's real capitalization so listed children carry the
            // corrected case (e.g. "D:/foo/" -> "D:/Foo/<child>").
            pathPrefix = caseCorrect(expandedPath, prefix);
            ensureTrailingSlash(pathPrefix);
        }
        else
        {
            // Path is a directory but doesn't end with /, treat as complete match
            // and offer trailing slash, correcting the typed case (e.g. "D:/foo" ->
            // "D:/Foo/").
            std::string const completedDir = caseCorrect(expandedPath, prefix);
            std::vector<CompletionItem> results;
            results.push_back(
                CompletionItem { .text = completedDir + "/",
                                 .displayText = std::filesystem::path(completedDir).filename().string() + "/",
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
                // Use the on-disk capitalization (and upper-case drive letter on Windows)
                // so the completed prefix matches the real path, not the user's typed case.
                // Bypasses _fs for the same reason as caseCorrect() above.
                // Only canonicalize absolute paths: canonicalCasePath() resolves to an
                // absolute path, which would otherwise rewrite a relative prefix the user
                // typed (e.g. "foo/") into an absolute one.
                pathPrefix =
                    dir.is_absolute() ? platform::canonicalCasePath(dir) : platform::normalizePath(dir);
                ensureTrailingSlash(pathPrefix);
            }
        }
    }

    // Check if directory exists
    if (!_fs.isDirectory(dir))
        return {};

    return listDirectory(dir, filePrefix, pathPrefix);
}

bool FileCompleter::canHandle(CompletionContextType type) const
{
    return type == CompletionContextType::FilePath || type == CompletionContextType::Argument
           || type == CompletionContextType::Redirect;
}

std::filesystem::path FileCompleter::expandTilde(std::string_view path) const
{
    if (path.empty() || path[0] != '~')
        return std::filesystem::path(path);

    std::string result;

    if (path.size() == 1 || path[1] == '/')
    {
        // ~ or ~/... - current user's home, resolved through the environment
        // abstraction (HOME, then USERPROFILE on Windows) so home resolution is
        // centralized and matches the rest of the shell.
        auto const home = _env.homeDirectory();
        if (!home.has_value())
            return std::filesystem::path(path); // No home set: leave the path unchanged.

        result = platform::normalizePath(*home); // Forward slashes, matching the rest.
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

bool FileCompleter::isHidden(std::string_view name)
{
    return !name.empty() && name[0] == '.';
}

std::vector<CompletionItem> FileCompleter::listDirectory(std::filesystem::path const& dir,
                                                         std::string_view prefix,
                                                         std::string_view pathPrefix) const
{
    std::vector<CompletionItem> results;

    // Determine if we should show hidden files
    bool showHidden = !prefix.empty() && prefix[0] == '.';

    // Configuration for fuzzy matching
    tui::FuzzyConfig fuzzyConfig;
    double const minThreshold = fuzzyConfig.minMatchThreshold;

    // On case-insensitive filesystems (Windows, default macOS) path matching must ignore
    // case so a wrong-case prefix still finds — and recases — the real entry (e.g.
    // "Lastrada-to" -> "lastrada-tools/"). The candidate text is built from the on-disk
    // filename below, so casing is corrected automatically. POSIX is case-sensitive, so
    // smart-case matching is kept there.
    bool const caseInsensitive = platform::FilesystemCaseInsensitive;

    auto const listing = _fs.listDirectory(dir);
    if (!listing)
        return results;

    for (auto const& entry: *listing)
    {
        auto filename = entry.path.filename().string();

        // Skip hidden files unless prefix starts with dot
        if (isHidden(filename) && !showHidden)
            continue;

        // Option C: Check both prefix and fuzzy matches
        bool isPrefixMatch = caseInsensitive
                                 ? tui::SmartCaseMatch::matchesPrefixCaseInsensitive(filename, prefix)
                                 : tui::SmartCaseMatch::matchesPrefix(filename, prefix);
        tui::FuzzyMatchResult fuzzyResult;
        bool isFuzzyMatch = false;

        if (!isPrefixMatch && !prefix.empty())
        {
            // Try fuzzy matching only if not a prefix match
            fuzzyResult = caseInsensitive ? tui::FuzzyMatch::match(filename, prefix, /*caseSensitive=*/false)
                                          : tui::FuzzyMatch::matchSmartCase(filename, prefix);
            size_t textLen = tui::FuzzyMatch::countGraphemes(filename);
            isFuzzyMatch =
                fuzzyResult.matches
                && (fuzzyResult.quality(textLen) >= minThreshold || fuzzyResult.isContiguousSubstring());
        }

        // Skip if neither prefix nor fuzzy match
        if (!isPrefixMatch && !isFuzzyMatch)
            continue;

        bool const isDir = entry.isDirectory;
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
