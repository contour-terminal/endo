// SPDX-License-Identifier: Apache-2.0
#include "CommandCompleter.hpp"
#include <shell/Shell.hpp>
#include <shell/completion/CompletionAdapter.hpp>

#include <endo-language/builtins/BuiltinSignatures.hpp>
#include <endo-language/ide/CompletionCandidates.hpp>

#include <crispy/utils.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <set>
#include <unordered_map>

#include <platform/PathUtils.hpp>

namespace endo
{

CommandCompleter::CommandCompleter(EnvironmentProvider const& env, History const& history):
    _env(env), _history(history)
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

    // Apply recency bonus from history: recently-used commands rank higher
    auto const& historyEntries = _history.entries(); // oldest first
    auto const total = static_cast<int>(historyEntries.size());
    if (total > 0)
    {
        constexpr auto MaxBonus = 200;
        auto recencyMap = std::unordered_map<std::string, int> {};
        for (auto i = total - 1; i >= 0; --i)
        {
            auto const& entry = historyEntries[static_cast<size_t>(i)];
            // Extract first word (the command name)
            auto const spacePos = entry.find(' ');
            auto const cmd = entry.substr(0, spacePos);
            if (!cmd.empty())
                recencyMap.try_emplace(cmd, MaxBonus * (i + 1) / total);
        }
        for (auto& item: results)
        {
            if (auto const it = recencyMap.find(item.text); it != recencyMap.end())
                item.score += it->second;
        }
    }

    // Sort by score (descending), then alphabetically
    std::ranges::sort(results, [](auto const& a, auto const& b) {
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

#if defined(_WIN32)
    auto const pathSep = ';';
#else
    auto const pathSep = ':';
#endif

    auto const paths = crispy::split(*pathValue, pathSep);

#if defined(_WIN32)
    // Build PATHEXT set for recognizing executable extensions
    auto execExts = std::set<std::string> {};
    if (auto const pathext = _env.get("PATHEXT"))
    {
        auto const exts = crispy::split(*pathext, ';');
        for (auto const& ext: exts)
        {
            std::string lower(ext);
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            execExts.insert(lower);
        }
    }
    else
    {
        execExts = { ".exe", ".cmd", ".bat", ".com" };
    }
#endif

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

            auto const& path = entry.path();

#if defined(_WIN32)
            // On Windows, check file extension against PATHEXT
            auto ext = path.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (!execExts.contains(ext))
                continue;

            // Use stem (without extension) as command name
            auto const cmdName = path.stem().string();
            commands.try_emplace(cmdName, platform::normalizePath(path));
#else
            // Check if executable (on POSIX)
            auto status = std::filesystem::status(path, ec);
            if (ec)
                continue;

            auto perms = status.permissions();
            auto const isExecutable =
                (perms & std::filesystem::perms::owner_exec) != std::filesystem::perms::none
                || (perms & std::filesystem::perms::group_exec) != std::filesystem::perms::none
                || (perms & std::filesystem::perms::others_exec) != std::filesystem::perms::none;

            if (isExecutable)
                commands.try_emplace(path.filename().string(), platform::normalizePath(path));
#endif
        }
    }

    return { commands.begin(), commands.end() };
}

std::vector<std::string> CommandCompleter::builtinNames()
{
    return endo::userFacingBuiltinNames();
}

} // namespace endo
