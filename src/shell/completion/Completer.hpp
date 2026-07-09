// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/completion/BuiltinArgumentCompleter.hpp>
#include <shell/completion/CommandCompleter.hpp>
#include <shell/completion/CommandSpecCompleter.hpp>
#include <shell/completion/CompletionProvider.hpp>
#include <shell/completion/FSharpCompleter.hpp>
#include <shell/completion/FileCompleter.hpp>
#include <shell/completion/HistoryCompleter.hpp>
#include <shell/completion/LetBindingCompleter.hpp>
#include <shell/completion/VariableCompleter.hpp>
#include <shell/history/History.hpp>

#include <endo-language/ide/CompletionContext.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <platform/EnvironmentProvider.hpp>
#include <platform/ProcessProvider.hpp>

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
    /// @param fs Optional filesystem used for required-paths validation in
    ///           history-based suggestions. Pass nullptr to disable validation.
    Completer(EnvironmentProvider const& env,
              History const& history,
              FSharpPersistentState const& fsharpState,
              FileSystem const* fs = nullptr);

    /// @brief Registers an additional completion provider.
    /// @param provider The provider to add.
    void addProvider(std::unique_ptr<CompletionProvider> provider);

    /// @brief Number of registered providers. For tests/diagnostics (e.g. asserting a
    ///        provider is not registered twice).
    [[nodiscard]] std::size_t providerCount() const { return _providers.size(); }

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
    [[nodiscard]] static CompletionContext analyzeContext(std::string_view input, size_t cursorPosition);

    /// @brief Takes and clears any errors from the last scripted completion execution.
    /// @return Formatted error messages, or empty if no scripted completer ran or no errors.
    [[nodiscard]] std::vector<std::string> takeLastErrors();

  private:
    /// Owned platform process provider used by the pkill process-name query.
    /// Kept alive for the lifetime of the CommandSpecCompleter that references it.
    std::unique_ptr<ProcessProvider> _processProvider;
    std::vector<std::unique_ptr<CompletionProvider>> _providers;
    CompletionConfig _config;

    /// @brief Gathers completions from all applicable providers.
    [[nodiscard]] std::vector<CompletionItem> gatherCompletions(CompletionContext const& ctx) const;

    /// @brief Finds the common prefix among completions.
    [[nodiscard]] static std::string findCommonPrefix(std::vector<CompletionItem> const& items);
};

} // namespace endo
