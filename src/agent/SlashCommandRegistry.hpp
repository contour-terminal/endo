// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include <agent/SlashCommand.hpp>

namespace endo::agent
{

/// @brief Registry for slash commands in agent mode.
///
/// Stores commands and provides lookup by name. Designed for dynamic registration —
/// skills, plugins, and other subsystems can register commands at runtime.
/// Linear vector storage — at ~10 commands, linear scan beats map overhead.
class SlashCommandRegistry
{
  public:
    SlashCommandRegistry() = default;
    ~SlashCommandRegistry() = default;

    SlashCommandRegistry(SlashCommandRegistry const&) = delete;
    SlashCommandRegistry& operator=(SlashCommandRegistry const&) = delete;
    SlashCommandRegistry(SlashCommandRegistry&&) = default;
    SlashCommandRegistry& operator=(SlashCommandRegistry&&) = default;

    /// @brief Registers a slash command.
    /// @param command The command to register (ownership transferred).
    void registerCommand(std::unique_ptr<SlashCommand> command);

    /// @brief Finds a command by name (without the leading '/').
    /// @param name The command name to look up.
    /// @return Pointer to the command, or nullptr if not found.
    [[nodiscard]] SlashCommand const* findCommand(std::string_view name) const;

    /// @brief Returns a span over all registered commands.
    [[nodiscard]] std::span<std::unique_ptr<SlashCommand> const> commands() const;

  private:
    std::vector<std::unique_ptr<SlashCommand>> _commands;
};

} // namespace endo::agent
