// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/PromptModule.hpp>

namespace endo
{

/// @brief Prompt module that shows F# mode badge when functions are defined.
class FSharpModeModule final: public PromptModule
{
  public:
    [[nodiscard]] std::string_view id() const noexcept override { return "fsharp_mode"; }

    [[nodiscard]] PromptSegments evaluate(PromptContext const& ctx) const override;
    [[nodiscard]] bool shouldShow(PromptContext const& ctx) const override;
};

} // namespace endo
