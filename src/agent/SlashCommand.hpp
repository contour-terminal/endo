// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <string_view>
#include <variant>

namespace endo::agent
{

/// @brief Result type: rewrite the user query and send to processMessage().
struct PromptRewrite
{
    std::string prompt;
};

/// @brief Result type: enter plan mode with the given query.
struct PlanModeRequest
{
    std::string query;
};

/// @brief Result type: print text directly without LLM involvement.
struct DirectOutput
{
    std::string text;
};

/// @brief The result of executing a slash command.
using SlashCommandResult = std::variant<PromptRewrite, PlanModeRequest, DirectOutput>;

/// @brief Abstract interface for slash commands in agent mode.
///
/// Each command has a name (without the leading '/'), a description for help text,
/// and an execute method that returns a result variant controlling how the shell
/// should process the command output.
class SlashCommand
{
  public:
    virtual ~SlashCommand() = default;

    /// @brief Returns the command name without the leading '/'.
    [[nodiscard]] virtual std::string_view name() const = 0;

    /// @brief Returns a short description for help text and completion popup.
    [[nodiscard]] virtual std::string_view description() const = 0;

    /// @brief Executes the command with the given arguments.
    /// @param arguments The text after the command name (may be empty).
    /// @return A result variant indicating how the shell should handle the output.
    [[nodiscard]] virtual SlashCommandResult execute(std::string_view arguments) const = 0;
};

} // namespace endo::agent
