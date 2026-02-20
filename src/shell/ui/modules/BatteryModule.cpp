// SPDX-License-Identifier: Apache-2.0
#include "BatteryModule.hpp"

#include <tui/Theme.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace endo
{

namespace
{

    struct BatteryInfo
    {
        int percentage = -1;
        bool charging = false;
        bool valid = false;
    };

    [[nodiscard]] auto readBattery() -> BatteryInfo
    {
        auto info = BatteryInfo {};

        // Try BAT0, BAT1
        for (auto const* batName: { "BAT0", "BAT1" })
        {
            auto const basePath = std::filesystem::path("/sys/class/power_supply") / batName;
            if (!std::filesystem::exists(basePath / "capacity"))
                continue;

            if (auto fs = std::ifstream(basePath / "capacity"); fs)
            {
                fs >> info.percentage;
                info.valid = true;
            }

            if (auto fs = std::ifstream(basePath / "status"); fs)
            {
                auto status = std::string {};
                std::getline(fs, status);
                info.charging = (status == "Charging" || status == "Full");
            }

            if (info.valid)
                break;
        }

        return info;
    }

} // namespace

bool BatteryModule::shouldShow([[maybe_unused]] PromptContext const& ctx) const
{
    return readBattery().valid;
}

PromptSegments BatteryModule::evaluate(PromptContext const& ctx) const
{
    auto const info = readBattery();
    if (!info.valid)
        return {};

    // Choose icon based on level
    auto const* icon = "\xf0\x9f\x94\x8b"; // U+1F50B battery
    if (info.charging)
        icon = "\xe2\x9a\xa1"; // U+26A1 lightning bolt

    auto style = tui::Style {};
    if (ctx.theme)
    {
        if (info.percentage <= 20)
            style.fg = ctx.theme->promptColors.exitCode; // Red for low battery
        else if (info.percentage <= 50)
            style.fg = ctx.theme->promptColors.duration; // Orange for medium
        else
            style.fg = ctx.theme->promptColors.gitClean; // Green for high
    }

    return { PromptSegment { .text = std::string(icon) + " " + std::to_string(info.percentage) + "%",
                             .style = style } };
}

} // namespace endo
