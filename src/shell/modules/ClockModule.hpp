// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/PromptModule.hpp>

namespace endo
{

/// @brief Prompt module that displays the current time (HH:MM:SS).
class ClockModule final: public PromptModule
{
  public:
    [[nodiscard]] std::string_view id() const noexcept override { return "clock"; }

    [[nodiscard]] PromptSegments evaluate(PromptContext const& ctx) const override;

    [[nodiscard]] std::optional<std::chrono::milliseconds> refreshInterval() const override
    {
        return std::chrono::milliseconds(1000);
    }
};

} // namespace endo
