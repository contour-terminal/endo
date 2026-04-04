// SPDX-License-Identifier: Apache-2.0
#include "PathModule.hpp"
#include <shell/ui/PromptColorResolver.hpp>

#include <tui/Theme.hpp>

namespace endo
{

PromptSegments PathModule::evaluate(PromptContext const& ctx) const
{
    auto path = ctx.cwd;

    // Tilde-contract home directory
    if (!ctx.homePath.empty() && path.starts_with(ctx.homePath))
    {
        path = "~" + path.substr(ctx.homePath.size());
        if (path.size() > 1 && path[1] != '/')
            path = ctx.cwd; // Restore if not a clean prefix match
    }

    auto style = tui::Style {};
    if (ctx.resolvedColors)
        style.fg = ctx.resolvedColors->path.solid();
    else if (ctx.theme)
        style.fg = ctx.theme->promptColors.path;
    style.bold = true;

    return { PromptSegment { .text = std::move(path), .style = style } };
}

} // namespace endo
