// SPDX-License-Identifier: Apache-2.0
#include "CommandSpecCompleter.hpp"
#include <shell/completion/CompletionAdapter.hpp>

#include <endo-language/ide/CompletionContext.hpp>

#include <algorithm>

namespace endo
{

void CommandSpecCompleter::registerCommand(CommandSpec spec,
                                           std::unique_ptr<CommandQueryProvider> queryProvider)
{
    auto const name = spec.command;
    auto entry = RegisteredCommand {};
    entry.spec = std::move(spec);
    if (queryProvider)
        entry.cache.emplace(std::move(queryProvider));
    _commands[name] = std::move(entry);

    // Wire up alias resolver: resolves git aliases (e.g., "br" → "branch") to canonical subcommand names.
    auto& stored = _commands[name];
    if (stored.cache)
    {
        stored.aliasResolver = [&cache =
                                    *stored.cache](std::string_view alias) -> std::optional<std::string> {
            auto const& aliases = cache.query("aliases");
            for (auto const& entry: aliases)
            {
                if (entry.text != alias)
                    continue;
                // Description format: "alias: <command> [args...]"
                static constexpr std::string_view Prefix = "alias: ";
                if (!entry.description.starts_with(Prefix))
                    return std::nullopt;
                auto const rest = std::string_view(entry.description).substr(Prefix.size());
                // Shell aliases (starting with '!') are not subcommand mappings
                if (rest.starts_with('!'))
                    return std::nullopt;
                // Extract the first word as the canonical subcommand name
                auto const space = rest.find(' ');
                return std::string(rest.substr(0, space));
            }
            return std::nullopt;
        };
    }
}

bool CommandSpecCompleter::canHandle(CompletionContextType type) const
{
    return type == CompletionContextType::Argument || type == CompletionContextType::Option;
}

bool CommandSpecCompleter::isExclusiveFor(CompletionContext const& context) const
{
    if (!context.command.has_value())
        return false;

    auto it = _commands.find(*context.command);
    if (it == _commands.end())
        return false;

    auto const& cmd = it->second;
    auto const state = parseCommandLine(
        cmd.spec, context.fullInput, context.cursorPosition, context.prefix, cmd.aliasResolver);
    if (!state.has_value())
        return false;

    // Exclusive when completing option values that aren't file paths
    if (state->phase == CompletionPhase::OptionValue)
    {
        // Find the option to check if it's a Path type (which should still allow FileCompleter)
        auto findOpt = [&](std::vector<OptionDef> const& options) -> OptionDef const* {
            for (auto const& opt: options)
            {
                if ((!opt.longName.empty() && state->optionExpectingValue == opt.longName)
                    || (!opt.shortName.empty() && state->optionExpectingValue == opt.shortName))
                    return &opt;
            }
            return nullptr;
        };

        OptionDef const* optDef = findOpt(cmd.spec.globalOptions);
        if (!optDef)
        {
            auto const* sub = resolveSubcommand(cmd.spec, state->subcommandChain);
            if (sub)
                optDef = findOpt(sub->options);
        }

        // Path options should still allow FileCompleter, all others are exclusive
        return !optDef || optDef->valueKind != OptionValueKind::Path;
    }

    // Exclusive when completing DynamicQuery positional arguments, unless the prefix looks like
    // a file path -- commands whose arguments name a process or a program accept a path too.
    if (state->phase == CompletionPhase::Argument)
    {
        if (auto const* argDef = applicableArgDef(cmd.spec, *state))
        {
            if (argDef->kind == ArgKind::Subcommand)
                return true;
            if (argDef->kind == ArgKind::DynamicQuery)
                return !CompletionContextAnalyzer::looksLikeFilePath(context.prefix);
        }
    }

    return false;
}

std::vector<CompletionItem> CommandSpecCompleter::complete(CompletionContext const& context)
{
    if (!context.command.has_value())
        return {};

    auto it = _commands.find(*context.command);
    if (it == _commands.end())
        return {};

    auto& cmd = it->second;

    auto const state = parseCommandLine(
        cmd.spec, context.fullInput, context.cursorPosition, context.prefix, cmd.aliasResolver);
    if (!state.has_value())
        return {};

    switch (state->phase)
    {
        case CompletionPhase::Subcommand: return completeSubcommand(cmd, *state, context.prefix);
        case CompletionPhase::Option: return completeOption(cmd, *state, context.prefix);
        case CompletionPhase::OptionValue: return completeOptionValue(cmd, *state, context.prefix);
        case CompletionPhase::Argument: return completeArgument(cmd, *state, context.prefix);
    }
    return {};
}

std::vector<CompletionItem> CommandSpecCompleter::completeSubcommand(RegisteredCommand& cmd,
                                                                     CommandLineState const& state,
                                                                     std::string_view prefix)
{
    auto const* sub = resolveSubcommand(cmd.spec, state.subcommandChain);
    auto const& subcommands = sub ? sub->subcommands : cmd.spec.subcommands;

    auto candidates = std::vector<CompletionCandidate> {};
    candidates.reserve(subcommands.size());

    for (auto const& sc: subcommands)
    {
        candidates.push_back(CompletionCandidate {
            .text = sc.name,
            .description = sc.description,
            .kind = CompletionKind::Command,
        });
    }

    // Also add aliases as subcommand completions if available
    if (cmd.cache)
    {
        auto const& aliases = cmd.cache->query("aliases");
        for (auto const& alias: aliases)
        {
            candidates.push_back(CompletionCandidate {
                .text = alias.text,
                .description = alias.description,
                .kind = CompletionKind::Command,
            });
        }
    }

    return applyFuzzyScoring(candidates, prefix, 80);
}

std::vector<CompletionItem> CommandSpecCompleter::completeOption(RegisteredCommand const& cmd,
                                                                 CommandLineState const& state,
                                                                 std::string_view prefix)
{
    auto candidates = std::vector<CompletionCandidate> {};

    // Collect options from both global and active subcommand
    auto addOptions = [&](std::vector<OptionDef> const& options) {
        for (auto const& opt: options)
        {
            // Skip already-seen options
            auto alreadySeen = false;
            for (auto const& seen: state.seenOptions)
            {
                if ((!opt.longName.empty() && seen == opt.longName)
                    || (!opt.shortName.empty() && seen == opt.shortName))
                {
                    alreadySeen = true;
                    break;
                }
            }
            if (alreadySeen)
                continue;

            if (!opt.longName.empty())
            {
                candidates.push_back(CompletionCandidate {
                    .text = opt.longName,
                    .description = opt.description,
                    .kind = CompletionKind::Other,
                });
            }
            if (!opt.shortName.empty())
            {
                candidates.push_back(CompletionCandidate {
                    .text = opt.shortName,
                    .description = opt.description,
                    .kind = CompletionKind::Other,
                });
            }
        }
    };

    addOptions(cmd.spec.globalOptions);

    auto const* sub = resolveSubcommand(cmd.spec, state.subcommandChain);
    if (sub)
        addOptions(sub->options);

    return applyFuzzyScoring(candidates, prefix, 75);
}

std::vector<CompletionItem> CommandSpecCompleter::completeArgument(RegisteredCommand& cmd,
                                                                   CommandLineState const& state,
                                                                   std::string_view prefix)
{
    auto const* argDef = applicableArgDef(cmd.spec, state);
    if (!argDef)
        return {};

    switch (argDef->kind)
    {
        case ArgKind::DynamicQuery: {
            if (!cmd.cache || argDef->queryTag.empty())
                return {};
            // Check if any seen option overrides the default query tag
            auto effectiveTag = std::string_view { argDef->queryTag };
            for (auto const& [opt, tag]: argDef->optionQueryOverrides)
            {
                if (std::ranges::find(state.seenOptions, opt) != state.seenOptions.end())
                {
                    effectiveTag = tag;
                    break;
                }
            }
            auto const& results = cmd.cache->query(effectiveTag);
            return queryToCompletions(results, prefix, 80);
        }
        case ArgKind::Subcommand: {
            // Nested subcommands handled by completeSubcommand
            return {};
        }
        case ArgKind::Path:
        case ArgKind::Any:
            // Delegate to FileCompleter or return empty
            return {};
    }
    return {};
}

std::vector<CompletionItem> CommandSpecCompleter::completeOptionValue(RegisteredCommand& cmd,
                                                                      CommandLineState const& state,
                                                                      std::string_view prefix)
{
    auto const& optName = state.optionExpectingValue;

    // Find the option definition
    auto findOpt = [&](std::vector<OptionDef> const& options) -> OptionDef const* {
        for (auto const& opt: options)
        {
            if ((!opt.longName.empty() && optName == opt.longName)
                || (!opt.shortName.empty() && optName == opt.shortName))
                return &opt;
        }
        return nullptr;
    };

    OptionDef const* optDef = findOpt(cmd.spec.globalOptions);
    if (!optDef)
    {
        auto const* sub = resolveSubcommand(cmd.spec, state.subcommandChain);
        if (sub)
            optDef = findOpt(sub->options);
    }

    if (!optDef)
        return {};

    switch (optDef->valueKind)
    {
        case OptionValueKind::Enum: {
            auto candidates = std::vector<CompletionCandidate> {};
            for (auto const& val: optDef->enumValues)
            {
                candidates.push_back(CompletionCandidate {
                    .text = val,
                    .description = optDef->description,
                    .kind = CompletionKind::EnumValue,
                });
            }
            return applyFuzzyScoring(candidates, prefix, 85);
        }
        case OptionValueKind::DynamicQuery: {
            if (!cmd.cache || optDef->queryTag.empty())
                return {};
            auto const& results = cmd.cache->query(optDef->queryTag);
            return queryToCompletions(results, prefix, 80);
        }
        case OptionValueKind::Path:
        case OptionValueKind::String:
        case OptionValueKind::None: return {};
    }
    return {};
}

ArgDef const* CommandSpecCompleter::applicableArgDef(CommandSpec const& spec, CommandLineState const& state)
{
    auto const* sub = resolveSubcommand(spec, state.subcommandChain);
    auto const& argDefs = sub ? sub->positionalArgs : spec.positionalArgs;

    if (argDefs.empty())
        return nullptr;

    auto argIdx = state.positionalArgIndex;
    for (auto const& def: argDefs)
    {
        // A repeatable ArgDef keeps applying to every position from its own onwards.
        if (argIdx == 0 || def.repeatable)
            return &def;
        --argIdx;
    }

    // Past all defined args: only a trailing repeatable one still applies.
    return argDefs.back().repeatable ? &argDefs.back() : nullptr;
}

SubcommandDef const* CommandSpecCompleter::resolveSubcommand(CommandSpec const& spec,
                                                             std::vector<std::string> const& chain)
{
    auto const* subs = &spec.subcommands;
    SubcommandDef const* current = nullptr;

    for (auto const& name: chain)
    {
        auto it = std::ranges::find_if(*subs, [&](SubcommandDef const& sub) { return sub.name == name; });
        if (it == subs->end())
            return nullptr;
        current = &*it;
        subs = &current->subcommands;
    }
    return current;
}

std::vector<CompletionItem> CommandSpecCompleter::queryToCompletions(std::vector<QueryResult> const& results,
                                                                     std::string_view prefix,
                                                                     int baseScore)
{
    auto candidates = std::vector<CompletionCandidate> {};
    candidates.reserve(results.size());
    for (auto const& r: results)
    {
        candidates.push_back(CompletionCandidate {
            .text = r.text,
            .description = r.description,
            .kind = CompletionKind::Other,
        });
    }
    return applyFuzzyScoring(candidates, prefix, baseScore);
}

} // namespace endo
