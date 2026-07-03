// SPDX-License-Identifier: Apache-2.0
#include <shell/commands/PkillCommand.hpp>
#include <shell/commands/ProcessMatch.hpp>
#include <shell/commands/TimeoutCommand.hpp>

#include <format>
#include <string_view>

namespace endo::pkill_cmd
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
            return std::unexpected(std::format("pkill: option requires an argument -- '{}'", flag));
        ++i;
        return args[i];
    }

} // namespace

std::expected<PkillOptions, std::string> parsePkillArgs(std::span<std::string const> args)
{
    auto opts = PkillOptions {};

    for (size_t i = 0; i < args.size(); ++i)
    {
        auto const& arg = args[i];

        if (arg == "-h" || arg == "--help")
        {
            opts.showHelp = true;
            return opts;
        }

        if (arg == "--")
        {
            if (i + 1 < args.size())
                opts.pattern = args[i + 1];
            if (i + 2 < args.size())
                return std::unexpected(std::string("pkill: too many arguments"));
            if (opts.pattern.empty())
                return std::unexpected(std::string("pkill: missing pattern"));
            return opts;
        }

        if (arg == "-f")
        {
            opts.fullMatch = true;
            continue;
        }
        if (arg == "-x")
        {
            opts.exactMatch = true;
            continue;
        }
        if (arg == "-i")
        {
            opts.caseInsensitive = true;
            continue;
        }
        if (arg == "-c")
        {
            opts.countOnly = true;
            continue;
        }
        if (arg == "-l")
        {
            opts.listOnly = true;
            continue;
        }
        if (arg == "-n")
        {
            opts.newestOnly = true;
            continue;
        }
        if (arg == "-o")
        {
            opts.oldestOnly = true;
            continue;
        }

        if (arg == "-u")
        {
            auto val = takeValue(args, i, "u");
            if (!val.has_value())
                return std::unexpected(val.error());
            opts.userFilter = process_match::splitCommaList(*val);
            if (opts.userFilter.empty())
                return std::unexpected(std::string("pkill: -u requires at least one user"));
            continue;
        }

        if (arg == "-s")
        {
            auto val = takeValue(args, i, "s");
            if (!val.has_value())
                return std::unexpected(val.error());
            auto const sigResult = timeout::parseSignalSpec(*val);
            if (!sigResult.has_value())
                return std::unexpected(std::format("pkill: {}", sigResult.error()));
            opts.signal = *sigResult;
            continue;
        }

        // -SIGNAL (e.g., -9, -TERM, -SIGKILL)
        if (arg.starts_with("-") && arg.size() > 1 && arg[1] != '-')
        {
            auto const spec = std::string_view(arg).substr(1);
            auto const sigResult = timeout::parseSignalSpec(spec);
            if (sigResult.has_value())
            {
                opts.signal = *sigResult;
                continue;
            }
            return std::unexpected(std::format("pkill: invalid option '{}'", arg));
        }

        // First non-option positional is the pattern; any further positionals are an error.
        if (opts.pattern.empty())
        {
            opts.pattern = arg;
            continue;
        }
        return std::unexpected(std::string("pkill: too many arguments"));
    }

    if (opts.newestOnly && opts.oldestOnly)
        return std::unexpected(std::string("pkill: -n and -o are mutually exclusive"));

    if (opts.pattern.empty())
        return std::unexpected(std::string("pkill: missing pattern"));

    return opts;
}

} // namespace endo::pkill_cmd
