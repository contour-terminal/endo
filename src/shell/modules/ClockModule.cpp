// SPDX-License-Identifier: Apache-2.0
#include "ClockModule.hpp"

#include <chrono>
#include <format>

#include <tui/Theme.hpp>

namespace endo
{

PromptSegments ClockModule::evaluate(PromptContext const& ctx) const
{
    auto const now = std::chrono::system_clock::now();
    auto const time = std::chrono::system_clock::to_time_t(now);
    auto const* const tm = std::localtime(&time); // NOLINT(concurrency-mt-unsafe)

    auto text = std::format("{:02d}:{:02d}:{:02d}", tm->tm_hour, tm->tm_min, tm->tm_sec);

    auto style = tui::Style {};
    if (ctx.theme)
        style.fg = ctx.theme->promptColors.clock;
    style.dim = true;

    return { PromptSegment { .text = std::move(text), .style = style } };
}

} // namespace endo
