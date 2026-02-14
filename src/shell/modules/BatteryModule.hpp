// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/PromptModule.hpp>

namespace endo
{

/// @brief Prompt module that displays battery percentage on Linux.
///
/// Reads from /sys/class/power_supply/BAT0/ or BAT1. Returns empty
/// on systems without a battery.
class BatteryModule final: public PromptModule
{
  public:
    [[nodiscard]] std::string_view id() const noexcept override { return "battery"; }

    [[nodiscard]] PromptSegments evaluate(PromptContext const& ctx) const override;
    [[nodiscard]] bool shouldShow(PromptContext const& ctx) const override;

    [[nodiscard]] std::optional<std::chrono::milliseconds> refreshInterval() const override
    {
        return std::chrono::seconds(30);
    }
};

} // namespace endo
