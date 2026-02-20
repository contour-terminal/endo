// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/ui/PromptModule.hpp>

namespace endo
{

/// @brief Prompt module that shows hostname when running under SSH.
class HostnameModule final: public PromptModule
{
  public:
    [[nodiscard]] std::string_view id() const noexcept override { return "hostname"; }

    [[nodiscard]] PromptSegments evaluate(PromptContext const& ctx) const override;
    [[nodiscard]] bool shouldShow(PromptContext const& ctx) const override;
};

} // namespace endo
