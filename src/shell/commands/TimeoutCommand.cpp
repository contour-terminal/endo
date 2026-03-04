// SPDX-License-Identifier: Apache-2.0
#include <shell/commands/TimeoutCommand.hpp>

#include <charconv>
#include <format>
#include <unordered_map>

namespace endo::timeout
{

namespace
{
    // clang-format off
    const std::unordered_map<std::string_view, int> signalNames = {
        { "HUP",     1 }, { "SIGHUP",     1 },
        { "INT",     2 }, { "SIGINT",     2 },
        { "QUIT",    3 }, { "SIGQUIT",    3 },
        { "ILL",     4 }, { "SIGILL",     4 },
        { "TRAP",    5 }, { "SIGTRAP",    5 },
        { "ABRT",    6 }, { "SIGABRT",    6 },
        { "BUS",     7 }, { "SIGBUS",     7 },
        { "FPE",     8 }, { "SIGFPE",     8 },
        { "KILL",    9 }, { "SIGKILL",    9 },
        { "USR1",   10 }, { "SIGUSR1",   10 },
        { "SEGV",   11 }, { "SIGSEGV",   11 },
        { "USR2",   12 }, { "SIGUSR2",   12 },
        { "PIPE",   13 }, { "SIGPIPE",   13 },
        { "ALRM",   14 }, { "SIGALRM",   14 },
        { "TERM",   15 }, { "SIGTERM",   15 },
    };
    // clang-format on

    /// Tries to consume an option value from an --option=value or next arg.
    std::expected<std::string, std::string> consumeOptionValue(std::string_view name,
                                                               std::string_view equalsValue,
                                                               std::span<std::string const> args,
                                                               size_t& i)
    {
        if (!equalsValue.empty())
            return std::string(equalsValue);
        if (i + 1 < args.size())
            return std::string(args[++i]);
        return std::unexpected(std::format("{} requires a value", name));
    }

} // namespace

std::expected<double, std::string> parseDuration(std::string_view str)
{
    if (str.empty())
        return std::unexpected(std::string("empty duration"));

    // Find where the numeric part ends
    auto numEnd = str.size();
    double multiplier = 1.0;

    if (!str.empty() && std::isalpha(static_cast<unsigned char>(str.back())))
    {
        auto const suffix = str.back();
        numEnd = str.size() - 1;
        switch (suffix)
        {
            case 's': multiplier = 1.0; break;
            case 'm': multiplier = 60.0; break;
            case 'h': multiplier = 3600.0; break;
            case 'd': multiplier = 86400.0; break;
            default: return std::unexpected(std::format("invalid duration suffix '{}'", suffix));
        }
    }

    auto const numPart = str.substr(0, numEnd);
    if (numPart.empty())
        return std::unexpected(std::string("missing numeric value in duration"));

    double value = 0.0;
    auto const [ptr, ec] = std::from_chars(numPart.data(), numPart.data() + numPart.size(), value);
    if (ec != std::errc {} || ptr != numPart.data() + numPart.size())
        return std::unexpected(std::format("invalid duration: '{}'", str));

    if (value < 0.0)
        return std::unexpected(std::format("negative duration: '{}'", str));

    return value * multiplier;
}

std::expected<int, std::string> parseSignalSpec(std::string_view str)
{
    if (str.empty())
        return std::unexpected(std::string("empty signal specification"));

    // Try numeric first
    int sigNum = 0;
    auto const [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), sigNum);
    if (ec == std::errc {} && ptr == str.data() + str.size())
    {
        if (sigNum < 1 || sigNum > 64)
            return std::unexpected(std::format("signal number out of range: {}", sigNum));
        return sigNum;
    }

    // Try name lookup
    auto const it = signalNames.find(str);
    if (it != signalNames.end())
        return it->second;

    return std::unexpected(std::format("unknown signal: '{}'", str));
}

std::expected<TimeoutOptions, std::string> parseTimeoutArgs(std::span<std::string const> args)
{
    auto opts = TimeoutOptions {};
    auto durationSeen = false;
    auto optionsDone = false;

    for (auto i = size_t { 0 }; i < args.size(); ++i)
    {
        auto const& arg = args[i];

        // After --, everything is part of the command
        if (optionsDone || (!arg.starts_with("-") && !durationSeen))
        {
            if (!durationSeen)
            {
                auto const dur = parseDuration(arg);
                if (!dur.has_value())
                    return std::unexpected(dur.error());
                opts.durationSeconds = dur.value();
                durationSeen = true;
                continue;
            }
            opts.command.push_back(arg);
            continue;
        }

        if (arg == "--")
        {
            optionsDone = true;
            continue;
        }

        // Once duration is seen, remaining args go to command
        if (durationSeen)
        {
            opts.command.push_back(arg);
            continue;
        }

        // Parse options (only before DURATION)
        auto equalsValue = std::string_view {};
        auto optName = std::string_view(arg);
        if (auto const eq = arg.find('='); eq != std::string::npos && arg.starts_with("--"))
        {
            optName = std::string_view(arg).substr(0, eq);
            equalsValue = std::string_view(arg).substr(eq + 1);
        }

        if (optName == "--signal" || optName == "-s")
        {
            auto val = consumeOptionValue(optName, equalsValue, args, i);
            if (!val.has_value())
                return std::unexpected(val.error());
            auto sig = parseSignalSpec(val.value());
            if (!sig.has_value())
                return std::unexpected(sig.error());
            opts.signal = sig.value();
        }
        else if (optName == "--kill-after" || optName == "-k")
        {
            auto val = consumeOptionValue(optName, equalsValue, args, i);
            if (!val.has_value())
                return std::unexpected(val.error());
            auto dur = parseDuration(val.value());
            if (!dur.has_value())
                return std::unexpected(dur.error());
            opts.killAfterSeconds = dur.value();
        }
        else if (arg == "--preserve-status")
        {
            opts.preserveStatus = true;
        }
        else if (arg == "--foreground")
        {
            opts.foreground = true;
        }
        else if (arg == "-v" || arg == "--verbose")
        {
            opts.verbose = true;
        }
        else if (arg == "-h" || arg == "--help")
        {
            opts.showHelp = true;
            return opts;
        }
        else
        {
            // Unknown option — treat as start of DURATION
            auto const dur = parseDuration(arg);
            if (!dur.has_value())
                return std::unexpected(std::format("unrecognized option: '{}'", arg));
            opts.durationSeconds = dur.value();
            durationSeen = true;
        }
    }

    if (opts.showHelp)
        return opts;

    if (!durationSeen)
        return std::unexpected(std::string("missing duration"));

    if (opts.command.empty())
        return std::unexpected(std::string("missing command"));

    return opts;
}

} // namespace endo::timeout
