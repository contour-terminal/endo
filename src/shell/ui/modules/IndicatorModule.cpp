// SPDX-License-Identifier: Apache-2.0
#include "IndicatorModule.hpp"
#include <shell/ui/PromptColorResolver.hpp>

#include <tui/Theme.hpp>

namespace endo
{

PromptSegments IndicatorModule::evaluate(PromptContext const& ctx) const
{
    auto style = tui::Style {};
    if (ctx.resolvedColors)
    {
        style.fg = (ctx.lastExitCode != 0) ? ctx.resolvedColors->indicatorError.solid()
                                           : ctx.resolvedColors->indicator.solid();
    }
    else if (ctx.theme)
    {
        style.fg = (ctx.lastExitCode != 0) ? ctx.theme->promptColors.indicatorError
                                           : ctx.theme->promptColors.indicator;
    }

    // Prefer the dynamically-resolved override (from a user-assigned function
    // value via shell_prompt_indicator) when present; otherwise fall back to
    // the static indicator string configured on this module.
    return { PromptSegment { .text = ctx.indicatorOverride.value_or(_indicator), .style = style } };
}

} // namespace endo
