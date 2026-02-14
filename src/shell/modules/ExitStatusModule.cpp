// SPDX-License-Identifier: Apache-2.0
#include "ExitStatusModule.hpp"

#include <tui/Theme.hpp>

#include <string>

namespace endo
{

bool ExitStatusModule::shouldShow(PromptContext const& ctx) const
{
    return ctx.lastExitCode != 0;
}

PromptSegments ExitStatusModule::evaluate(PromptContext const& ctx) const
{
    auto style = tui::Style {};
    if (ctx.theme)
        style.fg = ctx.theme->promptColors.exitCode;
    style.bold = true;

    return { PromptSegment { .text = "[" + std::to_string(ctx.lastExitCode) + "]", .style = style } };
}

} // namespace endo
