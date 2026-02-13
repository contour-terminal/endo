// SPDX-License-Identifier: Apache-2.0
#include "DurationModule.hpp"

#include <format>

#include <tui/Theme.hpp>

namespace endo
{

bool DurationModule::shouldShow(PromptContext const& ctx) const
{
    return ctx.lastDuration.count() >= _thresholdMs;
}

PromptSegments DurationModule::evaluate(PromptContext const& ctx) const
{
    auto const ms = ctx.lastDuration.count();
    auto text = std::string {};

    if (ms >= 60'000)
    {
        auto const minutes = ms / 60'000;
        auto const seconds = (ms % 60'000) / 1000;
        text = std::format("\xe2\x8f\xb1 {}m{}s", minutes, seconds); // U+23F1 stopwatch
    }
    else
    {
        auto const seconds = static_cast<double>(ms) / 1000.0;
        text = std::format("\xe2\x8f\xb1 {:.1f}s", seconds);
    }

    auto style = tui::Style {};
    if (ctx.theme)
        style.fg = ctx.theme->promptColors.duration;

    return { PromptSegment { .text = std::move(text), .style = style } };
}

} // namespace endo
