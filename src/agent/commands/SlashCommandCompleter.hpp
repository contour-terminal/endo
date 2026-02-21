// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tui/completer/CompletionProvider.hpp>

namespace endo::agent
{

class SlashCommandRegistry;

/// @brief Completion provider for slash commands in agent mode.
///
/// Implements tui::CompletionProvider to generate completions when the user
/// types a '/' prefix. Uses smart-case and fuzzy matching against all registered
/// commands in the registry. Dynamically added commands appear immediately since
/// the registry is read on each completion request.
///
/// Also provides argument completion for specific commands (e.g. `/model <name>`).
class SlashCommandCompleter final: public tui::CompletionProvider
{
  public:
    /// @brief Constructs a completer backed by the given command registry.
    /// @param registry The registry to read commands from (must outlive this object).
    explicit SlashCommandCompleter(SlashCommandRegistry const& registry);

    /// @brief Generates completions for slash command input.
    /// @param input The full input text.
    /// @param cursorPosition The cursor byte offset in the input.
    /// @return Completion items for matching commands, or empty if not applicable.
    [[nodiscard]] std::vector<tui::CompletionItem> complete(std::string_view input,
                                                            size_t cursorPosition) override;

    /// @brief Returns high priority so slash commands appear before other completions.
    [[nodiscard]] int priority() const override { return 100; }

  private:
    /// @brief Generates model name completions for `/model <prefix>`.
    /// @param prefix The prefix to filter model names by.
    /// @return Completion items for matching model names.
    [[nodiscard]] std::vector<tui::CompletionItem> completeModelArgument(std::string_view prefix);

    SlashCommandRegistry const& _registry;
};

} // namespace endo::agent
