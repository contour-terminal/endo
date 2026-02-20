// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <functional>
#include <string>
#include <string_view>

#include <agent/commands/SlashCommand.hpp>

namespace endo::agent
{

class SlashCommandRegistry;

/// @brief Registers core built-in slash commands (/help, /plan).
/// @param registry The registry to register commands into.
void registerBuiltinSlashCommands(SlashCommandRegistry& registry);

/// @brief Slash command implemented via a callback — for lambda-based dynamic registration.
///
/// Allows skills and plugins to register commands without defining custom classes:
/// @code
///   registry.registerCommand(std::make_unique<CallbackSlashCommand>(
///       "commit", "Generate a git commit", [](std::string_view args) {
///           return PromptRewrite { .prompt = "Review staged changes..." };
///       }));
/// @endcode
class CallbackSlashCommand final: public SlashCommand
{
  public:
    /// @brief Constructs a callback-based slash command.
    /// @param name The command name (without leading '/').
    /// @param description A short description for help text.
    /// @param handler The callback that executes the command.
    CallbackSlashCommand(std::string name,
                         std::string description,
                         std::function<SlashCommandResult(std::string_view)> handler);

    [[nodiscard]] std::string_view name() const override;
    [[nodiscard]] std::string_view description() const override;
    [[nodiscard]] SlashCommandResult execute(std::string_view arguments) const override;

  private:
    std::string _name;
    std::string _description;
    std::function<SlashCommandResult(std::string_view)> _handler;
};

} // namespace endo::agent
