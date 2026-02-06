// SPDX-License-Identifier: Apache-2.0
#include "CommandCompleter.hpp"

#include <crispy/utils.h>

#include <algorithm>
#include <filesystem>
#include <set>

#include "Shell.hpp"

namespace endo
{

CommandCompleter::CommandCompleter(Environment const& env): _env(env)
{
}

std::vector<CompletionItem> CommandCompleter::complete(CompletionContext const& context)
{
    refreshCacheIfNeeded();

    std::vector<CompletionItem> results;
    auto const& prefix = context.prefix;

    // Add builtins first
    for (auto const& builtin: builtinNames())
    {
        if (builtin.starts_with(prefix))
        {
            results.push_back(CompletionItem {
                .text = builtin,
                .displayText = builtin,
                .description = "builtin",
                .score = 100 // Builtins get higher priority
            });
        }
    }

    // Add commands from PATH
    for (auto const& cmd: _cachedCommands)
    {
        if (cmd.starts_with(prefix))
        {
            // Check if already added (avoid duplicates with builtins)
            bool isDuplicate = false;
            for (auto const& existing: results)
            {
                if (existing.text == cmd)
                {
                    isDuplicate = true;
                    break;
                }
            }

            if (!isDuplicate)
            {
                results.push_back(
                    CompletionItem { .text = cmd, .displayText = cmd, .description = "", .score = 50 });
            }
        }
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

std::vector<std::string> CommandCompleter::scanPath() const
{
    std::set<std::string> commands; // Use set for deduplication

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
            bool isExecutable =
                (perms & std::filesystem::perms::owner_exec) != std::filesystem::perms::none
                || (perms & std::filesystem::perms::group_exec) != std::filesystem::perms::none
                || (perms & std::filesystem::perms::others_exec) != std::filesystem::perms::none;

            if (isExecutable)
                commands.insert(path.filename().string());
        }
    }

    return std::vector<std::string>(commands.begin(), commands.end());
}

std::vector<std::string> CommandCompleter::builtinNames()
{
    return {
        // Shell builtins
        "cd",
        "exit",
        "export",
        "set",
        "unset",
        "read",
        "true",
        "false",
        "jobs",
        "fg",
        "bg",
        "wait",
        // Control flow keywords (also completable)
        "if",
        "then",
        "else",
        "elif",
        "fi",
        "for",
        "while",
        "do",
        "done",
        "case",
        "esac",
        "in",
        "function",
        "return",
        "break",
        "continue",
    };
}

} // namespace endo
