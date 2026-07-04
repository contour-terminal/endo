// SPDX-License-Identifier: Apache-2.0
#include <shell/builtins/InlineArgParser.hpp>

#include <algorithm>
#include <format>
#include <string>

namespace endo
{

bool ParsedInlineArgs::hasFlag(std::string_view flag) const
{
    return std::ranges::any_of(flags, [flag](auto const& p) { return p.first == flag; });
}

std::optional<std::string_view> ParsedInlineArgs::getFlagValue(std::string_view flag) const
{
    for (auto const& [f, v]: flags)
    {
        if (f == flag)
            return std::string_view(v);
    }
    return std::nullopt;
}

namespace
{

    /// Find the option definition matching a short flag character (e.g., 'r' matches "-r").
    InlineOptionDef const* findShortOption(char ch, std::span<InlineOptionDef const> options)
    {
        for (auto const& opt: options)
        {
            if (opt.shortFlag.size() == 2 && opt.shortFlag[1] == ch)
                return &opt;
        }
        return nullptr;
    }

    /// Find the option definition matching a long flag.
    InlineOptionDef const* findLongOption(std::string_view longFlag, std::span<InlineOptionDef const> options)
    {
        for (auto const& opt: options)
        {
            if (!opt.longFlag.empty() && opt.longFlag == longFlag)
                return &opt;
        }
        return nullptr;
    }

    /// Get the canonical flag name for an option (prefer short, fallback to long).
    std::string_view canonicalFlag(InlineOptionDef const& opt)
    {
        return opt.shortFlag.empty() ? opt.longFlag : opt.shortFlag;
    }

} // namespace

ParsedInlineArgs parseInlineArgs(CoreVM::CoreStringArray const& args,
                                 std::span<InlineOptionDef const> options)
{
    ParsedInlineArgs result;
    bool endOfOptions = false;

    for (size_t i = 1; i < args.size(); ++i)
    {
        auto const arg = std::string_view(args.at(i));

        // After --, everything is positional
        if (endOfOptions)
        {
            result.positionalArgs.emplace_back(arg);
            continue;
        }

        // End-of-options marker
        if (arg == "--")
        {
            endOfOptions = true;
            continue;
        }

        // Help flags (always recognized)
        if (arg == "-h" || arg == "--help")
        {
            result.helpRequested = true;
            return result;
        }

        // Long flags: --flag or --flag=value
        if (arg.starts_with("--") && arg.size() > 2)
        {
            auto const eqPos = arg.find('=');
            auto const flagName = (eqPos != std::string_view::npos) ? arg.substr(0, eqPos) : arg;
            auto const* opt = findLongOption(flagName, options);
            if (opt)
            {
                if (opt->takesValue)
                {
                    if (eqPos != std::string_view::npos)
                    {
                        result.flags.emplace_back(canonicalFlag(*opt), std::string(arg.substr(eqPos + 1)));
                    }
                    else if (i + 1 < args.size())
                    {
                        result.flags.emplace_back(canonicalFlag(*opt), std::string(args.at(++i)));
                    }
                    // else: missing value, treat as positional
                }
                else
                {
                    result.flags.emplace_back(canonicalFlag(*opt), std::string {});
                }
                continue;
            }
            // Unknown long flag — treat as positional
            result.positionalArgs.emplace_back(arg);
            continue;
        }

        // Short flags: -x or combined -rfv
        if (arg.starts_with("-") && arg.size() > 1 && arg[1] != '-')
        {
            // Two-pass: validate all chars first, then add flags
            // This prevents partial recording when e.g. -rx has valid -r but invalid -x
            struct PendingFlag
            {
                InlineOptionDef const* opt;
                size_t charIndex;
            };

            std::vector<PendingFlag> pending;
            bool allValid = true;
            bool hasValueFlag = false;

            for (size_t j = 1; j < arg.size() && !hasValueFlag; ++j)
            {
                auto const* opt = findShortOption(arg[j], options);
                if (!opt)
                {
                    allValid = false;
                    break;
                }
                pending.push_back({ .opt = opt, .charIndex = j });
                if (opt->takesValue)
                {
                    hasValueFlag = true;
                }
            }

            if (allValid && !pending.empty())
            {
                for (auto const& [opt, idx]: pending)
                {
                    if (opt->takesValue)
                    {
                        if (idx + 1 < arg.size())
                            result.flags.emplace_back(canonicalFlag(*opt), std::string(arg.substr(idx + 1)));
                        else if (i + 1 < args.size())
                            result.flags.emplace_back(canonicalFlag(*opt), std::string(args.at(++i)));
                    }
                    else
                    {
                        result.flags.emplace_back(canonicalFlag(*opt), std::string {});
                    }
                }
                continue;
            }

            // Unknown short flag — treat whole arg as positional
            result.positionalArgs.emplace_back(arg);
            continue;
        }

        // Plain positional argument
        result.positionalArgs.emplace_back(arg);
    }

    return result;
}

// ---------------------------------------------------------------------------
// Help text generation
// ---------------------------------------------------------------------------

std::string generateInlineHelp(InlineCommandDescriptor const& desc)
{
    std::string md;
    md.reserve(512);

    // Title
    md += "# ";
    md += desc.name;
    md += "\n\n";

    // Description
    md += desc.briefDescription;
    md += "\n\n";

    // Usage
    md += "## Usage\n\n`";
    md += desc.usageLine;
    md += "`\n";

    // Options table (if any options beyond implicit -h/--help)
    if (!desc.options.empty())
    {
        md += "\n## Options\n\n";
        md += "| Option | Description |\n";
        md += "|--------|-------------|\n";

        for (auto const& opt: desc.options)
        {
            md += "| ";
            if (!opt.shortFlag.empty() && !opt.longFlag.empty())
            {
                md += '`';
                md += opt.shortFlag;
                if (opt.takesValue)
                    md += " VALUE";
                md += "`, `";
                md += opt.longFlag;
                if (opt.takesValue)
                    md += "=VALUE";
                md += '`';
            }
            else if (!opt.shortFlag.empty())
            {
                md += '`';
                md += opt.shortFlag;
                if (opt.takesValue)
                    md += " VALUE";
                md += '`';
            }
            else
            {
                md += '`';
                md += opt.longFlag;
                if (opt.takesValue)
                    md += "=VALUE";
                md += '`';
            }
            md += " | ";
            md += opt.description;
            md += " |\n";
        }

        // Always add -h/--help
        md += "| `-h`, `--help` | Display this help |\n";
    }

    return md;
}

// ---------------------------------------------------------------------------
// Completion spec generation
// ---------------------------------------------------------------------------

std::vector<CommandSpec> generateBuiltinCompletionSpecs(std::span<InlineCommandDescriptor const> descriptors)
{
    std::vector<CommandSpec> specs;
    specs.reserve(descriptors.size());

    for (auto const& desc: descriptors)
    {
        CommandSpec spec;
        spec.command = std::string(desc.name);
        spec.description = std::string(desc.briefDescription);

        // Convert options
        for (auto const& opt: desc.options)
        {
            OptionDef optDef;
            optDef.longName = std::string(opt.longFlag);
            optDef.shortName = std::string(opt.shortFlag);
            optDef.description = std::string(opt.description);
            optDef.valueKind = opt.takesValue ? OptionValueKind::String : OptionValueKind::None;
            spec.globalOptions.push_back(std::move(optDef));
        }

        // Always add --help
        spec.globalOptions.push_back(
            OptionDef { .longName = "--help", .shortName = "-h", .description = "Show help" });

        // Positional args
        if (!desc.positionalQuery.queryTag.empty())
        {
            auto overrides = std::vector<std::pair<std::string, std::string>> {};
            if (!desc.positionalQuery.overrideFlag.empty())
                overrides.emplace_back(std::string(desc.positionalQuery.overrideFlag),
                                       std::string(desc.positionalQuery.overrideQueryTag));
            spec.positionalArgs.push_back(
                ArgDef { .kind = ArgKind::DynamicQuery,
                         .description = std::string(desc.positionalQuery.description),
                         .queryTag = std::string(desc.positionalQuery.queryTag),
                         .repeatable = desc.positionalQuery.repeatable,
                         .optionQueryOverrides = std::move(overrides) });
        }
        else if (desc.acceptsFileArgs)
        {
            spec.positionalArgs.push_back(ArgDef {
                .kind = ArgKind::Path, .description = "File(s)", .repeatable = desc.fileArgsRepeatable });
        }

        specs.push_back(std::move(spec));
    }

    return specs;
}

// ---------------------------------------------------------------------------
// LSP/completion builtin info generation
// ---------------------------------------------------------------------------

std::vector<BuiltinInfo> inlineBuiltinInfos(std::span<InlineCommandDescriptor const> descriptors)
{
    std::vector<BuiltinInfo> infos;
    infos.reserve(descriptors.size());

    for (auto const& desc: descriptors)
    {
        auto detail = std::format("**{}** -- builtin\n\n{}", desc.name, desc.briefDescription);
        infos.push_back(BuiltinInfo {
            .name = std::string(desc.name),
            .description = "builtin",
            .isProperty = false,
            .detail = std::move(detail),
        });
    }

    return infos;
}

} // namespace endo
