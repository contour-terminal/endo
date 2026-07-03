// SPDX-License-Identifier: Apache-2.0
#include <shell/commands/PidofCommand.hpp>
#include <shell/commands/ProcessMatch.hpp>

#include <algorithm>
#include <charconv>
#include <format>
#include <ranges>

#include <platform/PathUtils.hpp>

namespace endo::pidof_cmd
{

namespace
{

    /// Consumes the value for an option that requires one, returning the consumed string
    /// or an error describing the missing operand.
    std::expected<std::string, std::string> takeValue(std::span<std::string const> args,
                                                      size_t& i,
                                                      std::string_view flag)
    {
        if (i + 1 >= args.size())
            return std::unexpected(std::format("pidof: option requires an argument -- '{}'", flag));
        ++i;
        return args[i];
    }

    /// Parses a comma-separated PID list (e.g. "1234,5678").
    std::expected<std::vector<int64_t>, std::string> parseOmitPids(std::string_view spec)
    {
        auto pids = std::vector<int64_t> {};
        for (auto const& token: process_match::splitCommaList(spec))
        {
            int64_t pid = 0;
            auto const [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), pid);
            if (ec != std::errc {} || ptr != token.data() + token.size())
                return std::unexpected(std::format("pidof: invalid process id '{}'", token));
            pids.push_back(pid);
        }
        if (pids.empty())
            return std::unexpected(std::string("pidof: -o requires at least one process id"));
        return pids;
    }

    /// Returns the path basename of @p command. Both '/' and '\\' count as
    /// separators on every host, so Windows-style entries behave identically
    /// across platforms (std::filesystem::path::filename would not split on
    /// backslash on POSIX).
    [[nodiscard]] std::string_view basenameOf(std::string_view command)
    {
        auto const pos = command.find_last_of("/\\");
        return pos == std::string_view::npos ? command : command.substr(pos + 1);
    }

    /// Compares two names for equality, case-insensitively if the policy says so.
    [[nodiscard]] bool namesEqual(std::string_view a, std::string_view b, NameMatchPolicy policy)
    {
        return policy.caseInsensitive ? platform::equalsCaseInsensitive(a, b) : a == b;
    }

    /// Returns @p name with a trailing ".exe" removed (case-insensitively), if present.
    [[nodiscard]] std::string_view stripExeSuffix(std::string_view name)
    {
        constexpr auto ExeSuffix = std::string_view(".exe");
        if (name.size() > ExeSuffix.size()
            && platform::equalsCaseInsensitive(name.substr(name.size() - ExeSuffix.size()), ExeSuffix))
            return name.substr(0, name.size() - ExeSuffix.size());
        return name;
    }

} // namespace

std::expected<PidofOptions, std::string> parsePidofArgs(std::span<std::string const> args)
{
    auto opts = PidofOptions {};

    auto optionsEnded = false; // set once "--" is seen
    for (size_t i = 0; i < args.size(); ++i)
    {
        auto const& arg = args[i];

        if (!optionsEnded)
        {
            if (arg == "-h" || arg == "--help")
            {
                opts.showHelp = true;
                return opts;
            }
            if (arg == "--")
            {
                optionsEnded = true;
                continue;
            }
            if (arg == "-s")
            {
                opts.singleShot = true;
                continue;
            }
            if (arg == "-q")
            {
                opts.quiet = true;
                continue;
            }
            if (arg == "-S" || arg == "--separator" || arg == "-d")
            {
                auto val = takeValue(args, i, arg == "-d" ? "d" : "S");
                if (!val.has_value())
                    return std::unexpected(val.error());
                opts.separator = *val;
                continue;
            }
            if (arg == "-o")
            {
                auto val = takeValue(args, i, "o");
                if (!val.has_value())
                    return std::unexpected(val.error());
                auto pids = parseOmitPids(*val);
                if (!pids.has_value())
                    return std::unexpected(pids.error());
                opts.omitPids.insert(opts.omitPids.end(), pids->begin(), pids->end());
                continue;
            }
            if (arg.starts_with('-') && arg.size() > 1)
                return std::unexpected(std::format("pidof: invalid option '{}'", arg));
        }

        opts.programNames.push_back(arg);
    }

    if (opts.programNames.empty())
        return std::unexpected(std::string("pidof: missing program name"));

    return opts;
}

bool matchesProgramName(ProcessEntry const& entry, std::string_view name, NameMatchPolicy policy)
{
    auto const command = std::string_view(entry.command);
    auto const base = basenameOf(command);
    if (namesEqual(command, name, policy) || namesEqual(base, name, policy))
        return true;
    if (policy.stripExeSuffix)
        return namesEqual(stripExeSuffix(base), stripExeSuffix(name), policy);
    return false;
}

std::vector<int64_t> findPids(std::span<ProcessEntry const> entries,
                              PidofOptions const& opts,
                              NameMatchPolicy policy)
{
    auto const isRequested = [&](ProcessEntry const& entry) {
        if (std::ranges::find(opts.omitPids, entry.pid) != opts.omitPids.end())
            return false;
        return std::ranges::any_of(opts.programNames, [&](std::string const& name) {
            return matchesProgramName(entry, name, policy);
        });
    };

    auto pids = entries | std::views::filter(isRequested) | std::views::transform(&ProcessEntry::pid)
                | std::ranges::to<std::vector>();

    std::ranges::sort(pids, std::ranges::greater {});
    if (opts.singleShot && pids.size() > 1)
        pids.resize(1);
    return pids;
}

} // namespace endo::pidof_cmd
