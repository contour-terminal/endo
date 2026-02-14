// SPDX-License-Identifier: Apache-2.0
#include "CommandCompleter.hpp"
#include <shell/CompletionAdapter.hpp>
#include <shell/Shell.hpp>

#include <endo-language/CompletionCandidates.hpp>

#include <crispy/utils.h>

#include <algorithm>
#include <filesystem>
#include <map>

namespace endo
{

CommandCompleter::CommandCompleter(EnvironmentProvider const& env): _env(env)
{
}

std::vector<CompletionItem> CommandCompleter::complete(CompletionContext const& context)
{
    refreshCacheIfNeeded();

    auto const& prefix = context.prefix;

    // Get builtin candidates from shared engine
    auto builtins = builtinCandidates();

    // Build PATH command candidates with resolved path as description
    auto const homeValue = _env.get("HOME");
    auto const home = homeValue ? std::string(*homeValue) : std::string {};

    std::vector<CompletionCandidate> pathCandidates;
    pathCandidates.reserve(_cachedCommands.size());
    for (auto const& [cmd, fullPath]: _cachedCommands)
    {
        auto description = fullPath;
        if (!home.empty() && description.starts_with(home))
            description.replace(0, home.size(), "~");
        pathCandidates.push_back(CompletionCandidate { .text = cmd,
                                                       .displayText = cmd,
                                                       .description = std::move(description),
                                                       .detail = {},
                                                       .kind = CompletionKind::Command });
    }

    // Apply fuzzy scoring: builtins at higher base score
    auto results = applyFuzzyScoring(builtins, prefix, 100);

    // Apply fuzzy scoring: PATH commands at lower base score, merge in
    auto pathResults = applyFuzzyScoring(pathCandidates, prefix, 50);
    for (auto& item: pathResults)
    {
        auto isDuplicate = false;
        for (auto const& existing: results)
        {
            if (existing.text == item.text)
            {
                isDuplicate = true;
                break;
            }
        }
        if (!isDuplicate)
            results.push_back(std::move(item));
    }

    // Sort by score (descending), then alphabetically
    std::sort(results.begin(), results.end(), [](auto const& a, auto const& b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.text < b.text;
    });

    return results;
}

bool CommandCompleter::canHandle(CompletionContextType type) const
{
    return type == CompletionContextType::Command;
}

void CommandCompleter::invalidateCache()
{
    _cachedCommands.clear();
    _cachedPath.clear();
}

void CommandCompleter::refreshCacheIfNeeded() const
{
    auto pathValue = _env.get("PATH");
    std::string currentPath = pathValue ? std::string(*pathValue) : "";

    if (currentPath != _cachedPath)
    {
        _cachedPath = currentPath;
        _cachedCommands = scanPath();
    }
}

std::vector<std::pair<std::string, std::string>> CommandCompleter::scanPath() const
{
    std::map<std::string, std::string> commands; // name → full path, keeps first occurrence (PATH priority)

    auto pathValue = _env.get("PATH");
    if (!pathValue)
        return {};

    auto const paths = crispy::split(*pathValue, ':');

    for (auto const& pathStr: paths)
    {
        if (pathStr.empty())
            continue;

        std::error_code ec;
        std::filesystem::path dir(pathStr);

        if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec))
            continue;

        for (auto const& entry: std::filesystem::directory_iterator(dir, ec))
        {
            if (ec)
                break;

            if (!entry.is_regular_file(ec) && !entry.is_symlink(ec))
                continue;

            // Check if executable (on POSIX)
            auto const& path = entry.path();
            auto status = std::filesystem::status(path, ec);
            if (ec)
                continue;

            auto perms = status.permissions();
            auto const isExecutable =
                (perms & std::filesystem::perms::owner_exec) != std::filesystem::perms::none
                || (perms & std::filesystem::perms::group_exec) != std::filesystem::perms::none
                || (perms & std::filesystem::perms::others_exec) != std::filesystem::perms::none;

            if (isExecutable)
                commands.try_emplace(path.filename().string(), path.string());
        }
    }

    return { commands.begin(), commands.end() };
}

std::vector<std::string> CommandCompleter::builtinNames()
{
    return {
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
        // Control flow keywords (also completable)
        "if",
        "then",
        "else",
        "elif",
        "for",
        "while",
        "do",
        "end",
        "in",
        "return",
        "break",
        "continue",
    };
}

} // namespace endo
