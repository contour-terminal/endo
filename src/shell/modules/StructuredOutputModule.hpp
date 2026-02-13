// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/PromptModule.hpp>

namespace endo
{

/// @brief Prompt module that shows a badge when structured output commands are available.
class StructuredOutputModule final: public PromptModule
{
  public:
    [[nodiscard]] std::string_view id() const noexcept override { return "structured_output"; }
    [[nodiscard]] PromptSegments evaluate(PromptContext const& ctx) const override;
    [[nodiscard]] bool shouldShow(PromptContext const& ctx) const override;
};

} // namespace endo
