// SPDX-License-Identifier: Apache-2.0
#include "HostnameModule.hpp"

#include <tui/Theme.hpp>

namespace endo
{

bool HostnameModule::shouldShow(PromptContext const& ctx) const
{
    return ctx.isSSH;
}

PromptSegments HostnameModule::evaluate(PromptContext const& ctx) const
{
    auto style = tui::Style {};
    if (ctx.theme)
        style.fg = ctx.theme->promptColors.hostname;
    style.bold = true;

    return { PromptSegment { .text = ctx.hostname + ":", .style = style } };
}

} // namespace endo
