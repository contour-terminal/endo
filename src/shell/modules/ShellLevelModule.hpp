// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/PromptModule.hpp>

namespace endo
{

/// @brief Prompt module that shows the shell nesting level when inside a nested shell.
class ShellLevelModule final: public PromptModule
{
  public:
    [[nodiscard]] std::string_view id() const noexcept override { return "shell_level"; }

    [[nodiscard]] PromptSegments evaluate(PromptContext const& ctx) const override;
    [[nodiscard]] bool shouldShow(PromptContext const& ctx) const override;
};

} // namespace endo
