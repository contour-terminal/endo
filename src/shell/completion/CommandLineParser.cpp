// SPDX-License-Identifier: Apache-2.0
#include "CommandLineParser.hpp"

#include <algorithm>
#include <span>
#include <sstream>

namespace endo
{

namespace
{

    /// @brief Tokenizes input up to cursor position.
    [[nodiscard]] auto tokenize(std::string_view input) -> std::vector<std::string>
    {
        auto tokens = std::vector<std::string> {};
        auto iss = std::istringstream(std::string(input));
        auto token = std::string {};
        while (iss >> token)
            tokens.push_back(std::move(token));
        return tokens;
    }

    /// @brief Checks if a token matches any option's long or short name that consumes a value.
    [[nodiscard]] auto optionConsumesValue(std::span<OptionDef const> options, std::string_view token) -> bool
    {
        for (auto const& opt: options)
        {
            if (opt.valueKind == OptionValueKind::None)
                continue;
            if ((!opt.longName.empty() && token == opt.longName)
                || (!opt.shortName.empty() && token == opt.shortName))
                return true;
        }
        return false;
    }

    /// @brief Finds the OptionDef that matches a given token.
    [[nodiscard]] auto findOption(std::span<OptionDef const> options, std::string_view token)
        -> OptionDef const*
    {
        for (auto const& opt: options)
        {
            if ((!opt.longName.empty() && token == opt.longName)
                || (!opt.shortName.empty() && token == opt.shortName))
                return &opt;
        }
        return nullptr;
    }

    /// @brief Resolves the active SubcommandDef by walking a subcommand chain.
    [[nodiscard]] auto resolveSubcommand(CommandSpec const& spec, std::span<std::string const> chain)
        -> SubcommandDef const*
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

} // namespace

std::optional<CommandLineState> parseCommandLine(CommandSpec const& spec,
                                                 std::string_view fullInput,
                                                 size_t cursorPosition,
                                                 std::string_view prefix,
                                                 AliasResolver const& aliasResolver)
{
    auto const input = fullInput.substr(0, std::min(cursorPosition, fullInput.size()));
    auto const tokens = tokenize(input);

    if (tokens.empty() || tokens[0] != spec.command)
        return std::nullopt;

    auto state = CommandLineState {};
    state.command = tokens[0];

    auto const endsWithSpace = !input.empty() && (input.back() == ' ' || input.back() == '\t');

    // Track whether we're still looking for a subcommand at the current depth
    auto expectingSubcommand = !spec.subcommands.empty();
    auto skipNext = false;
    auto unmatchedTokenWasSubcommandCandidate = false;
    auto pendingOptionName = std::string {};   ///< Option awaiting its value (set when skipNext=true).
    auto lastValueOptionName = std::string {}; ///< Option that consumed the most recent skipNext token.

    SubcommandDef const* activeSub = nullptr;

    for (size_t i = 1; i < tokens.size(); ++i)
    {
        if (skipNext)
        {
            skipNext = false;
            lastValueOptionName = std::move(pendingOptionName);
            pendingOptionName.clear();
            continue;
        }
        lastValueOptionName.clear();

        auto const& tok = tokens[i];

        // Is it an option?
        if (tok.starts_with("-"))
        {
            state.seenOptions.push_back(tok);

            // Check if this option consumes the next token
            // Check both global and subcommand options
            auto consumesValue = optionConsumesValue(spec.globalOptions, tok);
            if (activeSub)
                consumesValue = consumesValue || optionConsumesValue(activeSub->options, tok);

            if (consumesValue)
            {
                // Check for --option=value form
                if (auto eqPos = tok.find('='); eqPos != std::string::npos)
                {
                    // Value is inline, no skip
                }
                else
                {
                    skipNext = true;
                    pendingOptionName = tok;
                }
            }
            continue;
        }

        // Try as subcommand
        if (expectingSubcommand)
        {
            auto resolved = tok;
            if (aliasResolver)
            {
                if (auto alias = aliasResolver(tok))
                    resolved = *alias;
            }

            auto const& searchIn = activeSub ? activeSub->subcommands : spec.subcommands;
            auto it = std::ranges::find_if(searchIn,
                                           [&](SubcommandDef const& sub) { return sub.name == resolved; });

            if (it != searchIn.end())
            {
                state.subcommandChain.push_back(resolved);
                activeSub = &*it;
                expectingSubcommand = !activeSub->subcommands.empty();
                continue;
            }
            // Not a known subcommand — treat as positional arg and stop looking for subcommands
            // But remember this token was in a subcommand-expecting position
            unmatchedTokenWasSubcommandCandidate = true;
            expectingSubcommand = false;
        }

        // It's a positional argument
        state.positionalArgs.push_back(tok);
    }

    // Determine the completion phase based on cursor position

    // If the last token was an option that consumes a value and we ran out of tokens
    if (skipNext)
    {
        // We skipped the value but there was no next token — we're completing the option value
        state.optionExpectingValue = tokens.back();
        state.phase = CompletionPhase::OptionValue;
        return state;
    }

    // If the last token was consumed as an option value and we're still typing it
    if (!endsWithSpace && !lastValueOptionName.empty())
    {
        state.optionExpectingValue = lastValueOptionName;
        state.phase = CompletionPhase::OptionValue;
        return state;
    }

    // If we're not at a space boundary, the prefix is the last token being typed
    if (!endsWithSpace && !tokens.empty() && tokens.size() > 1)
    {
        auto const& lastToken = tokens.back();

        // Typing an option?
        if (lastToken.starts_with("-"))
        {
            state.phase = CompletionPhase::Option;
            // Remove from seenOptions since it's still being typed
            if (!state.seenOptions.empty() && state.seenOptions.back() == lastToken)
                state.seenOptions.pop_back();
            return state;
        }

        // Typing a subcommand?
        if (expectingSubcommand || unmatchedTokenWasSubcommandCandidate)
        {
            // Remove from subcommandChain if it was added
            if (!state.subcommandChain.empty() && state.subcommandChain.back() == lastToken)
                state.subcommandChain.pop_back();
            // Remove from positionalArgs if the unmatched token was added there
            if (!state.positionalArgs.empty() && state.positionalArgs.back() == lastToken)
                state.positionalArgs.pop_back();
            state.phase = CompletionPhase::Subcommand;
            return state;
        }

        // Typing a positional argument
        if (!state.positionalArgs.empty() && state.positionalArgs.back() == lastToken)
            state.positionalArgs.pop_back();
        state.phase = CompletionPhase::Argument;
        state.positionalArgIndex = state.positionalArgs.size();
        return state;
    }

    // Cursor is at a space boundary — determine what comes next
    if (expectingSubcommand && !activeSub)
    {
        // No subcommand yet — offer subcommands
        state.phase = CompletionPhase::Subcommand;
    }
    else if (expectingSubcommand && activeSub && !activeSub->subcommands.empty())
    {
        // Has nested subcommands — could be subcommand or argument
        state.phase = CompletionPhase::Subcommand;
    }
    else
    {
        // Subcommand resolved, offer arguments
        state.phase = CompletionPhase::Argument;
        state.positionalArgIndex = state.positionalArgs.size();
    }

    return state;
}

} // namespace endo
