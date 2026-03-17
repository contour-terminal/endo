// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/ui/PromptModule.hpp>

namespace endo
{

/// @brief Prompt module that shows command execution duration when above threshold.
class DurationModule final: public PromptModule
{
  public:
    /// @brief Constructs a DurationModule with the given display threshold.
    /// @param thresholdMs Minimum duration in milliseconds before showing.
    explicit DurationModule(int64_t thresholdMs = 2000): _thresholdMs(thresholdMs) {}

    [[nodiscard]] std::string_view id() const noexcept override { return "duration"; }

    [[nodiscard]] ModuleSensitivity sensitivity() const override { return ModuleSensitivity::Duration; }

    [[nodiscard]] PromptSegments evaluate(PromptContext const& ctx) const override;
    [[nodiscard]] bool shouldShow(PromptContext const& ctx) const override;

  private:
    int64_t _thresholdMs;
};

} // namespace endo
