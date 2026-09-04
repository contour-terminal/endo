// SPDX-License-Identifier: Apache-2.0
#include "CommandCompleter.hpp"
#include <shell/Shell.hpp>
#include <shell/completion/CompletionAdapter.hpp>

#include <endo-language/builtins/BuiltinSignatures.hpp>
#include <endo-language/ide/CompletionCandidates.hpp>

#include <algorithm>
#include <unordered_map>

namespace endo
{

CommandCompleter::CommandCompleter(PathCommandIndex const& pathCommands,
                                   EnvironmentProvider const& env,
                                   History const& history):
    _pathCommands(pathCommands), _env(env), _history(history)
{
}

std::vector<CompletionItem> CommandCompleter::complete(CompletionContext const& context)
{
    auto const& prefix = context.prefix;

    // Get builtin candidates from shared engine
    auto builtins = builtinCandidates();

    // Build PATH command candidates with resolved path as description
    auto const& pathCommands = _pathCommands.entries();
    auto const home = homeDirectory(_env);

    std::vector<CompletionCandidate> pathCandidates;
    pathCandidates.reserve(pathCommands.size());
    for (auto const& [cmd, fullPath]: pathCommands)
        pathCandidates.push_back(CompletionCandidate { .text = cmd,
                                                       .displayText = cmd,
                                                       .description = collapseHomePrefix(fullPath, home),
                                                       .detail = {},
                                                       .kind = CompletionKind::Command });

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

std::vector<std::string> CommandCompleter::builtinNames()
{
    return endo::userFacingBuiltinNames();
}

} // namespace endo
