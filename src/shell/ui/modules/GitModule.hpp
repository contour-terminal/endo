// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/ui/PromptModule.hpp>

namespace endo
{

/// @brief Prompt module that displays Git branch and status information.
///
/// Shows the current branch name with color-coded dirty/clean/staged states.
/// Uses `git rev-parse` and `git status --porcelain=v2 --branch` for data.
class GitModule final: public PromptModule
{
  public:
    [[nodiscard]] std::string_view id() const noexcept override { return "git"; }

    [[nodiscard]] PromptSegments evaluate(PromptContext const& ctx) const override;
    [[nodiscard]] bool shouldShow(PromptContext const& ctx) const override;
};

} // namespace endo
