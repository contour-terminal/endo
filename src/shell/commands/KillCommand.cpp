// SPDX-License-Identifier: Apache-2.0
#include <shell/commands/KillCommand.hpp>
#include <shell/commands/TimeoutCommand.hpp>

#include <format>

namespace endo::kill_cmd
{

std::expected<KillOptions, std::string> parseKillArgs(std::span<std::string const> args)
{
    auto opts = KillOptions {};

    for (size_t i = 0; i < args.size(); ++i)
    {
        auto const& arg = args[i];

        if (arg == "-h" || arg == "--help")
        {
            opts.showHelp = true;
            return opts;
        }

        if (arg == "-l")
        {
            opts.listSignals = true;
            return opts;
        }

        // -s SIGNAL (POSIX style)
        if (arg == "-s")
        {
            if (i + 1 >= args.size())
                return std::unexpected(std::string("kill: -s requires a signal specification"));
            ++i;
            auto const sigResult = timeout::parseSignalSpec(args[i]);
            if (!sigResult.has_value())
                return std::unexpected(std::format("kill: {}", sigResult.error()));
            opts.signal = sigResult.value();
            continue;
        }

        // -SIGNAL (e.g., -9, -TERM, -SIGKILL)
        if (arg.starts_with("-") && arg.size() > 1 && arg[1] != '-')
        {
            auto const spec = std::string_view(arg).substr(1);

            // Check if it's a signal spec (numeric or named)
            auto const sigResult = timeout::parseSignalSpec(spec);
            if (sigResult.has_value())
            {
                opts.signal = sigResult.value();
                continue;
            }

            // Not a recognized signal — treat as error
            return std::unexpected(std::format("kill: invalid signal specification '{}'", spec));
        }

        // Everything else is a target (PID or %job_id)
        opts.targets.push_back(arg);
    }

    if (!opts.listSignals && !opts.showHelp && opts.targets.empty())
        return std::unexpected(std::string("kill: missing operand"));

    return opts;
}

} // namespace endo::kill_cmd
