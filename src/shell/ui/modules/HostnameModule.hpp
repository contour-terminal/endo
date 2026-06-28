// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/ui/PromptModule.hpp>

namespace endo
{

/// @brief Prompt module that shows the current `user@host` identity.
///
/// Renders the login name and hostname in fish-style `user@host` form to the left of
/// the working directory. Shown in every session (local and remote) so the prompt always
/// conveys who you are and which machine you're on. Hidden only when neither the username
/// nor the hostname could be determined.
class HostnameModule final: public PromptModule
{
  public:
    [[nodiscard]] std::string_view id() const noexcept override { return "hostname"; }

    [[nodiscard]] ModuleSensitivity sensitivity() const override { return ModuleSensitivity::None; }

    [[nodiscard]] PromptSegments evaluate(PromptContext const& ctx) const override;
    [[nodiscard]] bool shouldShow(PromptContext const& ctx) const override;
};

} // namespace endo
