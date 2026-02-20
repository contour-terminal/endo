// SPDX-License-Identifier: Apache-2.0
#include "FSharpModeModule.hpp"

#include <endo-language/codegen/IRGenerator.hpp>

#include <tui/Theme.hpp>

namespace endo
{

bool FSharpModeModule::shouldShow(PromptContext const& ctx) const
{
    return ctx.fsharpState && !ctx.fsharpState->functions.empty();
}

PromptSegments FSharpModeModule::evaluate(PromptContext const& ctx) const
{
    auto style = tui::Style {};
    if (ctx.theme)
    {
        style.fg = ctx.theme->promptColors.badgeText;
        style.bg = ctx.theme->promptColors.badge;
    }
    style.bold = true;

    // U+1D453 mathematical italic small f + # symbol
    return { PromptSegment { .text = " \xf0\x9d\x91\x93# ", .style = style } };
}

} // namespace endo
