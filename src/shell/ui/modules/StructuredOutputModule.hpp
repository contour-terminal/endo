// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/ui/PromptModule.hpp>

#include <optional>
#include <string>

namespace endo
{

/// @brief Prompt module that shows a badge when the typed command has structured output.
///
/// Reacts to user input in real-time. When the first command token matches a registered
/// structured command (ls, ps, git log, docker ps, etc.), displays a triangle icon (▷)
/// with the matched command name.
class StructuredOutputModule final: public PromptModule
{
  public:
    [[nodiscard]] std::string_view id() const noexcept override { return "structured_output"; }

    [[nodiscard]] ModuleSensitivity sensitivity() const override { return ModuleSensitivity::InputChange; }

    [[nodiscard]] PromptSegments evaluate(PromptContext const& ctx) const override;
    [[nodiscard]] bool shouldShow(PromptContext const& ctx) const override;

  private:
    /// @brief Matches the current input against registered structured commands.
    /// @return The display name of the matched command (e.g., "ls", "git log"), or nullopt.
    [[nodiscard]] static std::optional<std::string> matchStructuredCommand(PromptContext const& ctx);

    /// @brief Cached match result from the last shouldShow() call, reused by evaluate().
    mutable std::optional<std::string> _cachedMatch;
};

} // namespace endo
