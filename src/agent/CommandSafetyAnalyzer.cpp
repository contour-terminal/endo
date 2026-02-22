// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include <agent/CommandSafetyAnalyzer.hpp>

namespace endo::agent
{

namespace
{
    /// Extracts the base command name from a potentially qualified path.
    /// e.g. "/usr/bin/ls" → "ls", "ls -la" → "ls".
    auto extractBaseCommand(std::string_view segment) -> std::string_view
    {
        // Trim leading whitespace.
        while (!segment.empty() && (segment.front() == ' ' || segment.front() == '\t'))
            segment.remove_prefix(1);

        // Handle env prefix: "env VAR=val cmd" → skip to cmd.
        if (segment.starts_with("env "))
        {
            segment.remove_prefix(4);
            while (!segment.empty() && (segment.front() == ' ' || segment.front() == '\t'))
                segment.remove_prefix(1);
            // Skip VAR=val pairs.
            while (!segment.empty() && segment.find('=') < segment.find(' '))
            {
                auto const space = segment.find(' ');
                if (space == std::string_view::npos)
                    break;
                segment.remove_prefix(space + 1);
                while (!segment.empty() && (segment.front() == ' ' || segment.front() == '\t'))
                    segment.remove_prefix(1);
            }
        }

        // Handle sudo prefix.
        if (segment.starts_with("sudo "))
        {
            segment.remove_prefix(5);
            while (!segment.empty() && (segment.front() == ' ' || segment.front() == '\t'))
                segment.remove_prefix(1);
            // Skip sudo flags like -u, -E, etc.
            while (!segment.empty() && segment.front() == '-')
            {
                auto const space = segment.find(' ');
                if (space == std::string_view::npos)
                    break;
                segment.remove_prefix(space + 1);
                while (!segment.empty() && (segment.front() == ' ' || segment.front() == '\t'))
                    segment.remove_prefix(1);
            }
        }

        // Find end of command word.
        auto const space = segment.find(' ');
        auto const cmdPath = (space != std::string_view::npos) ? segment.substr(0, space) : segment;

        // Strip path prefix: "/usr/bin/ls" → "ls".
        auto const slash = cmdPath.rfind('/');
        return (slash != std::string_view::npos) ? cmdPath.substr(slash + 1) : cmdPath;
    }

    /// Splits a command string into segments by pipes and chain operators.
    auto splitCommandSegments(std::string_view command) -> std::vector<std::string_view>
    {
        auto segments = std::vector<std::string_view> {};
        auto start = size_t { 0 };
        auto inSingleQuote = false;
        auto inDoubleQuote = false;

        for (auto i = size_t { 0 }; i < command.size(); ++i)
        {
            auto const c = command[i];
            if (c == '\'' && !inDoubleQuote)
                inSingleQuote = !inSingleQuote;
            else if (c == '"' && !inSingleQuote)
                inDoubleQuote = !inDoubleQuote;
            else if (!inSingleQuote && !inDoubleQuote)
            {
                if (c == '|' || c == ';')
                {
                    if (i > start)
                        segments.push_back(command.substr(start, i - start));
                    start = i + 1;
                    // Skip "||" as a single operator.
                    if (c == '|' && i + 1 < command.size() && command[i + 1] == '|')
                        ++i, start = i + 1;
                }
                else if (c == '&')
                {
                    if (i > start)
                        segments.push_back(command.substr(start, i - start));
                    start = i + 1;
                    // Skip "&&" as a single operator.
                    if (i + 1 < command.size() && command[i + 1] == '&')
                        ++i, start = i + 1;
                }
            }
        }
        if (start < command.size())
            segments.push_back(command.substr(start));

        return segments;
    }

    // clang-format off
    constexpr auto ReadOnlyCommands = std::array {
        "ls", "cat", "head", "tail", "find", "which", "echo", "pwd", "tree",
        "stat", "wc", "du", "df", "date", "uname", "whoami", "env", "diff",
        "md5sum", "sha256sum", "ps", "free", "uptime", "nproc", "file", "type",
        "printenv", "hostname", "id", "groups", "test", "true", "false",
        "basename", "dirname", "realpath", "readlink", "sort", "uniq", "tr",
        "cut", "paste", "column", "tee", "xargs", "seq", "yes", "expr",
        "grep", "egrep", "fgrep", "rg", "ag", "fd", "bat", "exa", "jq", "yq",
    };

    constexpr auto MutatingCommands = std::array {
        "make", "cmake", "ninja", "cargo", "npm", "npx", "yarn", "pnpm",
        "pip", "pip3", "poetry", "pipenv", "conda",
        "cp", "mv", "mkdir", "touch", "chmod", "chown", "chgrp",
        "tar", "zip", "unzip", "gzip", "bzip2", "xz",
        "docker", "podman",
        "sed", "awk", "tee",
        "node", "ruby", "perl", "php",
        "gcc", "g++", "clang", "clang++", "rustc", "go",
        "ln", "install", "patch",
        "ctest", "pytest", "jest", "mocha",
    };

    constexpr auto InteractiveCommands = std::array {
        "vim", "vi", "nvim", "emacs", "nano", "pico", "joe", "micro",
        "top", "htop", "btop", "atop", "glances",
        "man", "info",
        "less", "more",
        "ssh", "telnet", "ftp", "sftp",
        "mysql", "psql", "sqlite3", "redis-cli", "mongo", "mongosh",
        "python", "python3",
        "irb", "ghci", "lua", "R",
        "gdb", "lldb",
    };
    // clang-format on

    /// Checks if a full command segment contains destructive patterns.
    auto hasDestructivePattern(std::string_view segment) -> bool
    {
        // rm -rf / or rm -rf /*
        if (segment.find("rm") != std::string_view::npos)
        {
            if (segment.find("-rf /") != std::string_view::npos
                || segment.find("-rf /*") != std::string_view::npos
                || segment.find("--no-preserve-root") != std::string_view::npos)
                return true;
        }

        // mkfs
        if (segment.find("mkfs") != std::string_view::npos)
            return true;

        // dd writing to block devices
        if (segment.find("dd ") != std::string_view::npos || segment.starts_with("dd "))
        {
            if (segment.find("of=/dev/") != std::string_view::npos)
                return true;
        }

        // Fork bombs: :(){ :|:& };:
        if (segment.find(":(){ :|:& };:") != std::string_view::npos)
            return true;
        if (segment.find("./$0|./$0&") != std::string_view::npos)
            return true;

        // Shutdown/reboot
        auto const baseCmd = extractBaseCommand(segment);
        if (baseCmd == "shutdown" || baseCmd == "reboot" || baseCmd == "poweroff" || baseCmd == "halt"
            || baseCmd == "init")
            return true;

        // chmod -R 777 /
        if (segment.find("chmod") != std::string_view::npos)
        {
            if (segment.find("-R") != std::string_view::npos
                && segment.find("777 /") != std::string_view::npos)
                return true;
        }

        // Destructive git operations (for shell_execute running git directly)
        if (segment.find("git") != std::string_view::npos)
        {
            if (segment.find("push --force") != std::string_view::npos
                || segment.find("push -f") != std::string_view::npos
                || segment.find("reset --hard") != std::string_view::npos
                || segment.find("clean -f") != std::string_view::npos)
                return true;
        }

        return false;
    }

    /// Checks if a command is interactive based on the base command and arguments.
    auto checkInteractive(std::string_view segment) -> bool
    {
        auto const baseCmd = extractBaseCommand(segment);
        for (auto const& cmd: InteractiveCommands)
        {
            if (baseCmd == cmd)
            {
                // Special cases: some interactive commands become non-interactive with arguments.
                if (baseCmd == "less" && segment.find('|') != std::string_view::npos)
                    return false; // less as pipe target is OK.
                if (baseCmd == "python" || baseCmd == "python3")
                {
                    // python with a script argument or -c flag is non-interactive.
                    if (segment.find(" -c ") != std::string_view::npos
                        || segment.find(".py") != std::string_view::npos)
                        return false;
                }
                if (baseCmd == "ssh")
                {
                    // ssh with a command is non-interactive.
                    // Count non-flag arguments: "ssh host command" has >=2 args after ssh.
                    auto argCount = 0;
                    auto rest = segment;
                    // Skip past "ssh"
                    if (auto const space = rest.find(' '); space != std::string_view::npos)
                        rest = rest.substr(space + 1);
                    else
                        return true;
                    // Simple heuristic: if there are multiple non-flag words, assume command follows host.
                    while (!rest.empty())
                    {
                        while (!rest.empty() && rest.front() == ' ')
                            rest.remove_prefix(1);
                        if (rest.empty())
                            break;
                        if (rest.front() != '-')
                            ++argCount;
                        else
                        {
                            // Skip flag and its value.
                            if (auto const space = rest.find(' '); space != std::string_view::npos)
                                rest = rest.substr(space + 1);
                            else
                                break;
                            continue;
                        }
                        if (auto const space = rest.find(' '); space != std::string_view::npos)
                            rest = rest.substr(space + 1);
                        else
                            break;
                    }
                    return argCount < 2; // Only host = interactive; host + command = non-interactive.
                }
                return true;
            }
        }

        // git rebase -i / git add -i / git add -p
        if (auto const baseCmd2 = extractBaseCommand(segment); baseCmd2 == "git")
        {
            if (segment.find("rebase -i") != std::string_view::npos
                || segment.find("rebase --interactive") != std::string_view::npos
                || segment.find("add -i") != std::string_view::npos
                || segment.find("add --interactive") != std::string_view::npos
                || segment.find("add -p") != std::string_view::npos
                || segment.find("add --patch") != std::string_view::npos)
                return true;
        }

        return false;
    }

    /// Classifies a single command segment.
    auto classifySegment(std::string_view segment, std::span<std::string const> extraBlockedPatterns)
        -> CommandAnalysis
    {
        // Trim whitespace.
        while (!segment.empty() && (segment.front() == ' ' || segment.front() == '\t'))
            segment.remove_prefix(1);
        while (!segment.empty() && (segment.back() == ' ' || segment.back() == '\t'))
            segment.remove_suffix(1);

        if (segment.empty())
            return { .risk = ToolRisk::ReadOnly, .reason = "Empty command", .isInteractive = false };

        // Check extra blocked patterns first.
        for (auto const& pattern: extraBlockedPatterns)
        {
            if (segment.find(pattern) != std::string_view::npos)
                return { .risk = ToolRisk::Blocked,
                         .reason = "Matches blocked pattern: " + pattern,
                         .isInteractive = false };
        }

        // Check interactive.
        if (checkInteractive(segment))
            return { .risk = ToolRisk::Blocked,
                     .reason = "Interactive command requires terminal",
                     .isInteractive = true };

        // Check destructive.
        if (hasDestructivePattern(segment))
            return { .risk = ToolRisk::Destructive,
                     .reason = "Destructive command pattern detected",
                     .isInteractive = false };

        // Check read-only.
        auto const baseCmd = extractBaseCommand(segment);
        for (auto const& cmd: ReadOnlyCommands)
        {
            if (baseCmd == cmd)
                return { .risk = ToolRisk::ReadOnly,
                         .reason = std::string(baseCmd) + " is a read-only command",
                         .isInteractive = false };
        }

        // Check known mutating.
        for (auto const& cmd: MutatingCommands)
        {
            if (baseCmd == cmd)
                return { .risk = ToolRisk::Mutating,
                         .reason = std::string(baseCmd) + " may modify files or state",
                         .isInteractive = false };
        }

        // Default: treat unknown commands as mutating.
        return { .risk = ToolRisk::Mutating,
                 .reason = "Unknown command defaults to mutating",
                 .isInteractive = false };
    }

} // namespace

auto CommandSafetyAnalyzer::classify(std::string_view command,
                                     std::span<std::string const> extraBlockedPatterns) -> CommandAnalysis
{
    if (command.empty())
        return { .risk = ToolRisk::ReadOnly, .reason = "Empty command", .isInteractive = false };

    auto const segments = splitCommandSegments(command);
    if (segments.empty())
        return { .risk = ToolRisk::ReadOnly, .reason = "Empty command", .isInteractive = false };

    // Classify each segment and return the highest risk.
    auto worstResult = CommandAnalysis { .risk = ToolRisk::ReadOnly,
                                         .reason = "All segments are safe",
                                         .isInteractive = false };

    for (auto const& segment: segments)
    {
        auto const result = classifySegment(segment, extraBlockedPatterns);
        if (static_cast<uint8_t>(result.risk) > static_cast<uint8_t>(worstResult.risk))
            worstResult = result;
        if (result.isInteractive)
            worstResult.isInteractive = true;
    }

    return worstResult;
}

auto CommandSafetyAnalyzer::isInteractive(std::string_view command) -> bool
{
    auto const segments = splitCommandSegments(command);
    return std::ranges::any_of(segments, [](auto const& seg) { return checkInteractive(seg); });
}

} // namespace endo::agent
