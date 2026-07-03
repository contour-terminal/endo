// SPDX-License-Identifier: Apache-2.0
#include <shell/commands/ProcessMatch.hpp>

#include <algorithm>
#include <format>
#include <regex>

namespace endo::process_match
{

std::vector<std::string> splitCommaList(std::string_view spec)
{
    auto tokens = std::vector<std::string> {};
    size_t start = 0;
    while (start <= spec.size())
    {
        auto const comma = spec.find(',', start);
        auto const end = (comma == std::string_view::npos) ? spec.size() : comma;
        if (end > start)
            tokens.emplace_back(spec.substr(start, end - start));
        if (comma == std::string_view::npos)
            break;
        start = comma + 1;
    }
    return tokens;
}

bool matchesUserFilter(ProcessEntry const& entry, std::vector<std::string> const& users)
{
    if (users.empty())
        return true;
    return std::ranges::find(users, entry.user) != users.end();
}

std::expected<std::vector<ProcessEntry>, std::string> filterProcesses(std::span<ProcessEntry const> entries,
                                                                      MatchOptions const& opts)
{
    auto flags = std::regex::ECMAScript;
    if (opts.caseInsensitive)
        flags |= std::regex::icase;

    auto const patternText = opts.exactMatch ? std::string("^(?:") + opts.pattern + ")$" : opts.pattern;

    auto pattern = std::regex {};
    try
    {
        pattern = std::regex(patternText, flags);
    }
    catch (std::regex_error const& ex)
    {
        return std::unexpected(std::format("invalid pattern '{}': {}", opts.pattern, ex.what()));
    }

    auto matches = std::vector<ProcessEntry> {};
    for (auto const& entry: entries)
    {
        if (opts.excludePid && entry.pid == *opts.excludePid)
            continue;
        if (!matchesUserFilter(entry, opts.userFilter))
            continue;

        // ProcessEntry::command is whatever the platform exposes most reliably
        // (argv[0] on Linux, the command string on Darwin/Windows).
        auto const& haystack = entry.command;
        auto const matched =
            opts.exactMatch ? std::regex_match(haystack, pattern) : std::regex_search(haystack, pattern);
        if (matched != opts.invert)
            matches.push_back(entry);
    }

    if (!matches.empty() && (opts.newestOnly || opts.oldestOnly))
    {
        auto const cmp = [](ProcessEntry const& a, ProcessEntry const& b) {
            return a.pid < b.pid;
        };
        auto const it =
            opts.newestOnly ? std::ranges::max_element(matches, cmp) : std::ranges::min_element(matches, cmp);
        auto picked = *it;
        matches.clear();
        matches.push_back(std::move(picked));
    }

    return matches;
}

} // namespace endo::process_match
