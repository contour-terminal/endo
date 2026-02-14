// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/PromptModule.hpp>

namespace endo
{

/// @brief Prompt module that shows the last command's exit code when non-zero.
class ExitStatusModule final: public PromptModule
{
  public:
    [[nodiscard]] std::string_view id() const noexcept override { return "exit_status"; }

    [[nodiscard]] PromptSegments evaluate(PromptContext const& ctx) const override;
    [[nodiscard]] bool shouldShow(PromptContext const& ctx) const override;
};

} // namespace endo
