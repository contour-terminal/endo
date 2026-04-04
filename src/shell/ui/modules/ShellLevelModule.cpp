// SPDX-License-Identifier: Apache-2.0
#include "ShellLevelModule.hpp"
#include <shell/ui/PromptColorResolver.hpp>

#include <tui/Theme.hpp>

#include <format>

namespace endo
{

bool ShellLevelModule::shouldShow(PromptContext const& ctx) const
{
    return ctx.shellLevel > 0;
}

PromptSegments ShellLevelModule::evaluate(PromptContext const& ctx) const
{
    auto style = tui::Style {};
    if (ctx.resolvedColors)
        style.fg = ctx.resolvedColors->hostname.solid();
    else if (ctx.theme)
        style.fg = ctx.theme->promptColors.hostname;
    style.bold = true;

    return { PromptSegment { .text = std::format("\u21A9 L{}", ctx.shellLevel), .style = style } };
}

} // namespace endo
