// SPDX-License-Identifier: Apache-2.0
#include "GitBranchCompleter.hpp"
#include <shell/CompletionAdapter.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <set>
#include <sstream>
#include <string>

namespace endo
{

namespace
{

    /// @brief Set of git subcommands that always expect a branch at any non-option arg position.
    constexpr std::array alwaysBranchSubcommands = {
        std::string_view { "checkout" },    std::string_view { "switch" }, std::string_view { "merge" },
        std::string_view { "rebase" },      std::string_view { "reset" },  std::string_view { "revert" },
        std::string_view { "cherry-pick" }, std::string_view { "log" },    std::string_view { "diff" },
        std::string_view { "show" },
    };

    /// @brief Set of git subcommands that expect a branch after a remote argument.
    constexpr std::array remoteFirstSubcommands = {
        std::string_view { "push" },
        std::string_view { "pull" },
        std::string_view { "fetch" },
    };

    /// @brief Runs a command and captures stdout lines.
    [[nodiscard]] auto runCommand(std::string const& cmd) -> std::vector<std::string>
    {
        auto lines = std::vector<std::string> {};
        auto* fp = popen(cmd.c_str(), "r"); // NOLINT(cert-env33-c)
        if (!fp)
            return lines;

        auto buf = std::array<char, 512> {};
        auto current = std::string {};
        while (fgets(buf.data(), static_cast<int>(buf.size()), fp) != nullptr)
        {
            current += buf.data();
            while (!current.empty() && (current.back() == '\n' || current.back() == '\r'))
                current.pop_back();
            if (!current.empty())
                lines.push_back(std::move(current));
            current.clear();
        }
        pclose(fp); // NOLINT(cert-env33-c)

        return lines;
    }

} // namespace

std::vector<CompletionItem> GitBranchCompleter::complete(CompletionContext const& context)
{
    if (!context.command.has_value() || *context.command != "git")
        return {};

    auto const info = parseGitCommand(context.fullInput, context.cursorPosition);
    if (!info.has_value() || info->subcommand.empty())
        return {};

    if (!expectsBranchAtPosition(*info))
        return {};

    auto const branches = queryBranches();
    if (branches.empty())
        return {};

    auto candidates = std::vector<CompletionCandidate> {};
    candidates.reserve(branches.size());
    for (auto const& branch: branches)
        candidates.push_back(CompletionCandidate {
            .text = branch,
            .description = "git branch",
            .kind = CompletionKind::Other,
        });

    return applyFuzzyScoring(candidates, context.prefix, 80);
}

bool GitBranchCompleter::canHandle(CompletionContextType type) const
{
    return type == CompletionContextType::Argument;
}

std::optional<GitBranchCompleter::GitCommandInfo> GitBranchCompleter::parseGitCommand(
    std::string_view fullInput, size_t cursorPosition)
{
    // Work with the portion of input up to the cursor
    auto const input = fullInput.substr(0, std::min(cursorPosition, fullInput.size()));

    // Tokenize by splitting on whitespace
    auto tokens = std::vector<std::string> {};
    auto iss = std::istringstream(std::string(input));
    auto token = std::string {};
    while (iss >> token)
        tokens.push_back(token);

    // Need at least "git" + subcommand
    if (tokens.empty() || tokens[0] != "git")
        return std::nullopt;

    if (tokens.size() < 2)
        return std::nullopt;

    auto info = GitCommandInfo {};

    // Git global options that consume the next token as their argument
    static constexpr std::array gitGlobalOptionsWithArg = {
        std::string_view { "-C" },
        std::string_view { "-c" },
        std::string_view { "--git-dir" },
        std::string_view { "--work-tree" },
    };

    // Find subcommand: first non-option token after "git"
    auto foundSubcommand = false;
    auto skipNext = false;
    for (size_t i = 1; i < tokens.size(); ++i)
    {
        if (skipNext)
        {
            skipNext = false;
            continue;
        }

        auto const& tok = tokens[i];

        if (tok.starts_with("-"))
        {
            // Track flags
            if (tok == "-d" || tok == "-D" || tok == "--delete")
                info.hasDeleteFlag = true;
            else if (tok == "-m" || tok == "-M" || tok == "--move")
                info.hasMoveFlag = true;

            // Check if this option consumes the next token
            for (auto const opt: gitGlobalOptionsWithArg)
            {
                if (tok == opt)
                {
                    skipNext = true;
                    break;
                }
            }
            continue;
        }

        if (!foundSubcommand)
        {
            info.subcommand = tok;
            foundSubcommand = true;
        }
        else
        {
            info.args.push_back(tok);
        }
    }

    // If the input ends with whitespace, cursor is at a new (empty) argument position.
    // If it doesn't, the last non-option token is the prefix being typed (already in context.prefix).
    // We only count fully completed args (those before the current prefix).
    if (!input.empty() && input.back() != ' ' && input.back() != '\t')
    {
        // The last token is the prefix being typed — don't count it as a completed arg
        if (!info.args.empty() && foundSubcommand)
            info.args.pop_back();
        else if (foundSubcommand && info.subcommand == tokens.back())
        {
            // User is still typing the subcommand itself, not an arg
            // But we still return the info so the caller can decide
        }
    }

    if (!foundSubcommand)
        return std::nullopt;

    return info;
}

bool GitBranchCompleter::expectsBranchAtPosition(GitCommandInfo const& info)
{
    auto const& sub = info.subcommand;

    // Always-branch subcommands: any non-option arg position
    for (auto const cmd: alwaysBranchSubcommands)
    {
        if (sub == cmd)
            return true;
    }

    // Remote-first subcommands: need remote (first arg) before offering branches
    for (auto const cmd: remoteFirstSubcommands)
    {
        if (sub == cmd)
            return !info.args.empty(); // args[0] is the remote
    }

    // `git branch` only expects branch with -d/-D/--delete or -m/-M/--move
    if (sub == "branch")
        return info.hasDeleteFlag || info.hasMoveFlag;

    return false;
}

std::vector<std::string> GitBranchCompleter::queryBranches()
{
    auto seen = std::set<std::string> {};
    auto result = std::vector<std::string> {};

    // Local branches
    auto const localBranches = runCommand("git branch --format='%(refname:short)' 2>/dev/null");
    for (auto const& branch: localBranches)
    {
        if (seen.insert(branch).second)
            result.push_back(branch);
    }

    // Remote tracking branches
    auto const remoteBranches = runCommand("git branch -r --format='%(refname:short)' 2>/dev/null");
    for (auto const& branch: remoteBranches)
    {
        // Strip "origin/" prefix for single-remote repos; keep full name otherwise
        auto stripped = branch;
        if (auto const slash = branch.find('/'); slash != std::string::npos)
            stripped = branch.substr(slash + 1);

        // Skip HEAD pointer entries like "origin/HEAD"
        if (stripped == "HEAD")
            continue;

        if (seen.insert(stripped).second)
            result.push_back(stripped);
    }

    std::ranges::sort(result);
    return result;
}

} // namespace endo
