// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/CompletionProvider.hpp>
#include <shell/CompletionProviders/BuiltinArgumentCompleter.hpp>
#include <shell/CompletionProviders/CommandCompleter.hpp>
#include <shell/CompletionProviders/FSharpCompleter.hpp>
#include <shell/CompletionProviders/FileCompleter.hpp>
#include <shell/CompletionProviders/HistoryCompleter.hpp>
#include <shell/CompletionProviders/LetBindingCompleter.hpp>
#include <shell/CompletionProviders/OptionCompleter.hpp>
#include <shell/CompletionProviders/VariableCompleter.hpp>
#include <shell/Environment.hpp>
#include <shell/History.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <endo-language/CompletionContext.hpp>

namespace endo
{

/// @brief Configuration for the completion system.
struct CompletionConfig
{
    bool insertCommonPrefix = false; ///< Insert common prefix before showing menu.
    size_t maxSuggestions = 50;      ///< Maximum completions to return.
};

/// @brief Orchestrates completion providers and generates suggestions.
///
/// The Completer is the main entry point for the completion system.
/// It manages multiple providers and combines their results.
class Completer
{
  public:
    /// @brief Constructs a completer with default providers.
    /// @param env The environment for variable and command completion.
    /// @param history The history for history-based suggestions.
    /// @param fsharpState The persistent F# state for let binding completion.
    Completer(Environment const& env, History const& history, FSharpPersistentState const& fsharpState);

    /// @brief Registers an additional completion provider.
    /// @param provider The provider to add.
    void addProvider(std::unique_ptr<CompletionProvider> provider);

    /// @brief Gets all completions for the current input state.
    /// @param input The input line.
    /// @param cursorPosition The cursor byte offset.
    /// @return List of completion items, sorted by relevance.
    [[nodiscard]] std::vector<CompletionItem> complete(std::string_view input, size_t cursorPosition) const;

    /// @brief Gets the best single suggestion for ghost text.
    /// @param input The input line.
    /// @param cursorPosition The cursor byte offset.
    /// @return The best suggestion suffix to append, or nullopt.
    [[nodiscard]] std::optional<std::string> suggest(std::string_view input, size_t cursorPosition) const;

    /// @brief Sets the completion configuration.
    void setConfig(CompletionConfig config);

    /// @brief Returns the current configuration.
    [[nodiscard]] CompletionConfig const& config() const;

    /// @brief Returns the analyzed context for the current input.
    /// @param input The input line.
    /// @param cursorPosition The cursor byte offset.
    [[nodiscard]] CompletionContext analyzeContext(std::string_view input, size_t cursorPosition) const;

  private:
    std::vector<std::unique_ptr<CompletionProvider>> _providers;
    CompletionConfig _config;

    /// @brief Gathers completions from all applicable providers.
    [[nodiscard]] std::vector<CompletionItem> gatherCompletions(CompletionContext const& ctx) const;

    /// @brief Finds the common prefix among completions.
    [[nodiscard]] static std::string findCommonPrefix(std::vector<CompletionItem> const& items);
};

} // namespace endo
