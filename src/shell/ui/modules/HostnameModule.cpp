// SPDX-License-Identifier: Apache-2.0
#include "HostnameModule.hpp"
#include <shell/ui/PromptColorResolver.hpp>

#include <tui/Theme.hpp>

namespace endo
{

bool HostnameModule::shouldShow(PromptContext const& ctx) const
{
    return !ctx.username.empty() || !ctx.hostname.empty();
}

PromptSegments HostnameModule::evaluate(PromptContext const& ctx) const
{
    /// @brief Builds a bold style carrying @p picker's foreground color.
    auto const styled = [&ctx](auto picker) {
        auto style = tui::Style {};
        if (ctx.resolvedColors)
            style.fg = picker(*ctx.resolvedColors).solid();
        else if (ctx.theme)
            style.fg = picker(ctx.theme->promptColors);
        style.bold = true;
        return style;
    };

    auto const usernameStyle = styled([](auto const& c) { return c.username; });
    auto const hostnameStyle = styled([](auto const& c) { return c.hostname; });

    // Fish-style `user@host`, with the user and host parts independently colored and
    // degrading gracefully when only one part is known. The `@` adopts the host color.
    auto segments = PromptSegments {};
    if (!ctx.username.empty())
        segments.push_back(PromptSegment { .text = ctx.username, .style = usernameStyle });
    if (!ctx.username.empty() && !ctx.hostname.empty())
        segments.push_back(PromptSegment { .text = "@", .style = hostnameStyle });
    if (!ctx.hostname.empty())
        segments.push_back(PromptSegment { .text = ctx.hostname, .style = hostnameStyle });

    return segments;
}

} // namespace endo
