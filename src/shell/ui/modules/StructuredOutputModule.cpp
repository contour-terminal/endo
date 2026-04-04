// SPDX-License-Identifier: Apache-2.0
#include "StructuredOutputModule.hpp"
#include <shell/ui/PromptColorResolver.hpp>

#include <endo-language/codegen/IRGenerator.hpp>

#include <tui/Theme.hpp>

namespace endo
{

bool StructuredOutputModule::shouldShow(PromptContext const& ctx) const
{
    if (!ctx.fsharpState || ctx.fsharpState->structuredCommands.empty() || ctx.currentInput.empty())
    {
        _cachedMatch.reset();
        return false;
    }

    _cachedMatch = matchStructuredCommand(ctx);
    return _cachedMatch.has_value();
}

PromptSegments StructuredOutputModule::evaluate(PromptContext const& ctx) const
{
    auto style = tui::Style {};
    if (ctx.resolvedColors)
    {
        style.fg = ctx.resolvedColors->badgeText.solid();
        style.bg = ctx.resolvedColors->badge.solid();
    }
    else if (ctx.theme)
    {
        style.fg = ctx.theme->promptColors.badgeText;
        style.bg = ctx.theme->promptColors.badge;
    }

    return { PromptSegment {
        .text = " \xe2\x96\xb7 " + _cachedMatch.value_or("") + " ", // U+25B7
        .style = style,
    } };
}

std::optional<std::string> StructuredOutputModule::matchStructuredCommand(PromptContext const& ctx)
{
    if (!ctx.fsharpState || ctx.currentInput.empty())
        return std::nullopt;

    auto const& commands = ctx.fsharpState->structuredCommands;
    auto const& input = ctx.currentInput;

    // Extract first whitespace-delimited token.
    auto const firstEnd = input.find_first_of(" \t");
    auto const cmd = input.substr(0, firstEnd);
    if (cmd.empty())
        return std::nullopt;

    // Check simple command match (e.g., "ls", "ps", "jobs", "bind").
    if (commands.contains(std::string(cmd)))
        return std::string(cmd);

    // Extract second token and check subcommand match (e.g., "git log", "docker ps").
    if (firstEnd != std::string::npos)
    {
        auto const secondStart = input.find_first_not_of(" \t", firstEnd);
        if (secondStart != std::string::npos)
        {
            auto const secondEnd = input.find_first_of(" \t", secondStart);
            auto const subcmd = input.substr(secondStart, secondEnd - secondStart);
            auto key = std::string(cmd) + '\0' + std::string(subcmd);
            if (commands.contains(key))
                return std::string(cmd) + " " + std::string(subcmd);
        }
    }

    return std::nullopt;
}

} // namespace endo
