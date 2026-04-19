// SPDX-License-Identifier: Apache-2.0
#include "RequiredPaths.hpp"

#include <algorithm>
#include <filesystem>
#include <string>

namespace endo
{

namespace
{

    /// Returns true when @p arg looks like a path (absolute, relative, or home-prefixed)
    /// rather than a flag or bare identifier.
    [[nodiscard]] bool looksLikePath(std::string_view arg)
    {
        if (arg.empty())
            return false;
        if (arg.front() == '-')
            return false; // option flag
        if (arg.front() == '/')
            return true;
        if (arg.starts_with("./") || arg.starts_with("../"))
            return true;
        if (arg == "~" || arg.starts_with("~/"))
            return true;
        return arg.find('/') != std::string_view::npos;
    }

    /// Resolves @p arg to an absolute path given @p cwdAbs and @p home.
    /// Does NOT call std::filesystem::canonical — missing targets stay valid.
    [[nodiscard]] std::string resolveToAbsolute(std::string_view arg,
                                                std::string_view cwdAbs,
                                                std::string_view home)
    {
        auto expanded = expandForLookup(arg, home);
        auto expandedPath = std::filesystem::path { expanded };
        if (expandedPath.is_absolute())
            return expandedPath.lexically_normal().string();

        auto combined = std::filesystem::path { cwdAbs } / expandedPath;
        return combined.lexically_normal().string();
    }

} // namespace

std::string canonicalizeForHistory(std::string_view absPath, std::string_view home)
{
    if (home.empty())
        return std::string { absPath };

    if (absPath == home)
        return "~";

    if (absPath.size() > home.size() && absPath.starts_with(home) && absPath[home.size()] == '/')
        return "~" + std::string { absPath.substr(home.size()) };

    return std::string { absPath };
}

std::string expandForLookup(std::string_view storedPath, std::string_view home)
{
    if (storedPath.empty() || home.empty())
        return std::string { storedPath };

    if (storedPath == "~")
        return std::string { home };

    if (storedPath.starts_with("~/"))
        return std::string { home } + std::string { storedPath.substr(1) };

    return std::string { storedPath };
}

std::vector<std::string> collectRequiredPaths(std::span<std::string const> argv,
                                              std::string_view cwdAbs,
                                              std::string_view home)
{
    auto result = std::vector<std::string> {};
    if (argv.size() <= 1)
        return result;

    for (auto const& arg: argv.subspan(1))
    {
        if (result.size() >= maxRequiredPaths)
            break;
        if (!looksLikePath(arg))
            continue;

        auto abs = resolveToAbsolute(arg, cwdAbs, home);
        auto canonical = canonicalizeForHistory(abs, home);

        // Deduplicate — identical paths in argv (e.g. `diff a a`) only count once.
        if (std::ranges::find(result, canonical) == result.end())
            result.push_back(std::move(canonical));
    }

    return result;
}

namespace
{

    /// Minimal shell-aware tokenizer used only for required-paths heuristics.
    /// Splits on whitespace; single/double quotes group a token (stripped).
    /// Does not interpret backslash escapes, variables, or redirections.
    [[nodiscard]] std::vector<std::string> tokenizeForPathScan(std::string_view line)
    {
        auto tokens = std::vector<std::string> {};
        auto current = std::string {};
        auto quote = char { 0 };

        auto flush = [&] {
            if (!current.empty())
            {
                tokens.push_back(std::move(current));
                current = std::string {};
            }
        };

        for (auto const c: line)
        {
            if (quote != 0)
            {
                if (c == quote)
                    quote = 0;
                else
                    current.push_back(c);
                continue;
            }
            if (c == '\'' || c == '"')
            {
                quote = c;
                continue;
            }
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            {
                flush();
                continue;
            }
            current.push_back(c);
        }
        flush();
        return tokens;
    }

} // namespace

std::vector<std::string> collectRequiredPathsFromCommandLine(std::string_view commandLine,
                                                             std::string_view cwdAbs,
                                                             std::string_view home)
{
    auto const tokens = tokenizeForPathScan(commandLine);
    return collectRequiredPaths(tokens, cwdAbs, home);
}

} // namespace endo
