// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/CompletionProvider.hpp>
#include <shell/CompletionProviders/CommandLineParser.hpp>
#include <shell/CompletionProviders/CommandQueryProvider.hpp>
#include <shell/CompletionProviders/CommandSpec.hpp>
#include <shell/CompletionProviders/QueryCache.hpp>

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace endo
{

/// @brief Completion provider driven by CommandSpec definitions.
///
/// Replaces GitBranchCompleter with a generic, data-driven system.
/// Commands register their spec + optional query provider.
/// Handles subcommand, option, and positional argument completion.
class CommandSpecCompleter: public CompletionProvider
{
  public:
    [[nodiscard]] std::vector<CompletionItem> complete(CompletionContext const& context) override;
    [[nodiscard]] bool canHandle(CompletionContextType type) const override;

    /// @brief Priority 85 — higher than FileCompleter (50), lower than CommandCompleter (100).
    [[nodiscard]] int priority() const override { return 85; }

    /// @brief Registers a command spec with an optional dynamic query provider.
    /// @param spec The command specification.
    /// @param queryProvider Optional provider for dynamic data (branches, containers, etc.).
    void registerCommand(CommandSpec spec, std::unique_ptr<CommandQueryProvider> queryProvider = nullptr);

  private:
    struct RegisteredCommand
    {
        CommandSpec spec;
        std::optional<QueryCache> cache; ///< Present if queryProvider was given.
        AliasResolver aliasResolver;     ///< Optional alias resolver for this command.
    };

    std::unordered_map<std::string, RegisteredCommand> _commands;

    /// @brief Completes subcommand names.
    [[nodiscard]] std::vector<CompletionItem> completeSubcommand(RegisteredCommand& cmd,
                                                                 CommandLineState const& state,
                                                                 std::string_view prefix);

    /// @brief Completes option flags (--flag / -f).
    [[nodiscard]] std::vector<CompletionItem> completeOption(RegisteredCommand const& cmd,
                                                             CommandLineState const& state,
                                                             std::string_view prefix);

    /// @brief Completes positional arguments.
    [[nodiscard]] std::vector<CompletionItem> completeArgument(RegisteredCommand& cmd,
                                                               CommandLineState const& state,
                                                               std::string_view prefix);

    /// @brief Completes the value of an option that takes one.
    [[nodiscard]] std::vector<CompletionItem> completeOptionValue(RegisteredCommand& cmd,
                                                                  CommandLineState const& state,
                                                                  std::string_view prefix);

    /// @brief Resolves the active SubcommandDef by walking subcommandChain.
    [[nodiscard]] static SubcommandDef const* resolveSubcommand(CommandSpec const& spec,
                                                                std::vector<std::string> const& chain);

    /// @brief Converts QueryResult items to CompletionItems with fuzzy scoring.
    [[nodiscard]] static std::vector<CompletionItem> queryToCompletions(
        std::vector<QueryResult> const& results, std::string_view prefix, int baseScore);
};

} // namespace endo
