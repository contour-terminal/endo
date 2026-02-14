// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/PromptModule.hpp>

namespace endo
{

/// @brief Prompt module that displays the current working directory.
///
/// Supports tilde-contraction (replacing home directory with ~) and
/// optional gradient coloring via PromptConfig.
class PathModule final: public PromptModule
{
  public:
    [[nodiscard]] std::string_view id() const noexcept override { return "path"; }

    [[nodiscard]] PromptSegments evaluate(PromptContext const& ctx) const override;
};

} // namespace endo
