// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/completion/CompleterFunctionRegistry.hpp>
#include <shell/completion/CompletionProvider.hpp>

#include <chrono>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace endo
{

/// @brief Result of executing a completer function, including any compilation errors.
struct CompleterExecutionResult
{
    std::vector<std::string> completions; ///< Completion candidate strings.
    std::vector<std::string> errors;      ///< Formatted diagnostic messages (if any).
};

/// @brief Callback type for executing an endo completer function.
///
/// @param funcName The function name to invoke (e.g., "flatpak_complete").
/// @param args Tokens after the command name, excluding the current word.
/// @param prefix The current word being typed (may be empty).
/// @return Completions and any compilation errors.
using CompleterExecutionCallback = std::function<CompleterExecutionResult(
    std::string_view funcName, std::vector<std::string> const& args, std::string_view prefix)>;

/// @brief Completion provider that delegates to endo-scripted completer functions.
///
/// Looks up the command in the CompleterFunctionRegistry, calls the registered function
/// via an execution callback, and converts results to scored CompletionItems.
/// Results are cached with a 2-second TTL keyed by (functionName, args).
class ScriptedCompleter: public CompletionProvider
{
  public:
    /// @brief Constructs a scripted completer.
    /// @param registry The function registry mapping commands to function names.
    /// @param callback The execution callback for invoking endo functions.
    ScriptedCompleter(CompleterFunctionRegistry const& registry, CompleterExecutionCallback callback);

    [[nodiscard]] std::vector<CompletionItem> complete(CompletionContext const& context) override;
    [[nodiscard]] bool canHandle(CompletionContextType type) const override;

    [[nodiscard]] int priority() const override { return 85; }

    [[nodiscard]] bool isExclusiveFor(CompletionContext const& context) const override;

    /// @brief Takes and clears any errors from the last completion execution.
    /// @return Formatted error messages from the last completer execution.
    [[nodiscard]] std::vector<std::string> takeLastErrors();

  private:
    CompleterFunctionRegistry const& _registry;
    CompleterExecutionCallback _callback;

    struct CacheEntry
    {
        std::vector<std::string> results;
        std::chrono::steady_clock::time_point timestamp;
    };

    static constexpr auto cacheTTL = std::chrono::milliseconds { 2000 };
    mutable std::unordered_map<std::string, CacheEntry> _cache;

    /// @brief Builds a cache key from function name and args.
    [[nodiscard]] static std::string makeCacheKey(std::string_view funcName,
                                                  std::vector<std::string> const& args);

    /// @brief Extracts argument tokens from the full input, excluding command and current word.
    [[nodiscard]] static std::vector<std::string> extractArgs(std::string_view fullInput,
                                                              std::string_view command,
                                                              std::string_view prefix);

    std::vector<std::string> _lastErrors; ///< Errors from the most recent completer execution.
};

} // namespace endo
