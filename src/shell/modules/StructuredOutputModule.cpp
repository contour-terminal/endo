// SPDX-License-Identifier: Apache-2.0
#include "StructuredOutputModule.hpp"

#include <endo-language/IRGenerator.hpp>
#include <shell/OutputDefinitionRegistry.hpp>
#include <tui/Theme.hpp>

namespace endo
{

bool StructuredOutputModule::shouldShow(PromptContext const& ctx) const
{
    return ctx.outputDefs && !ctx.outputDefs->definitions().empty();
}

PromptSegments StructuredOutputModule::evaluate(PromptContext const& ctx) const
{
    auto style = tui::Style {};
    if (ctx.theme)
    {
        style.fg = ctx.theme->promptColors.badgeText;
        style.bg = ctx.theme->promptColors.badge;
    }

    auto text = std::string { " \xe2\x96\xb7" }; // U+25B7 white right-pointing triangle

    if (ctx.fsharpState)
    {
        for (auto const& [cmd, _]: ctx.fsharpState->structuredCommands)
        {
            text += " " + cmd;
        }
    }

    text += " ";

    return { PromptSegment { .text = std::move(text), .style = style } };
}

} // namespace endo
