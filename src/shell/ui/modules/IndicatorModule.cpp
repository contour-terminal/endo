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

    return { PromptSegment { .text = _indicator, .style = style } };
}

} // namespace endo
