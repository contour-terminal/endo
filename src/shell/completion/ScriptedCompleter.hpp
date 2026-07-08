// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/completion/CompleterFunctionRegistry.hpp>
#include <shell/completion/CompletionProvider.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace endo
{

/// @brief A single collected completion with optional description and detail.
struct CollectedCompletion
{
    std::string text;        ///< The completion text to insert.
    std::string description; ///< Short description (shown right-aligned in menu).
    std::string detail;      ///< Longer markdown detail (shown in side panel).
};

/// @brief How a completer function's execution terminated.
///
/// A completer may run a `$(...)` command substitution (e.g. `$(dnf repoquery)`)
/// that the cancellable wait can abort (Ctrl+C) or time out. Such a run yields no
/// usable completions, so its (empty) result must NOT be cached — otherwise the
/// cache is poisoned with an empty entry and the next Tab keeps serving nothing.
enum class CompleterExecutionStatus : std::uint8_t
{
    Ok,       ///< Completed normally; result is authoritative and cacheable.
    Aborted,  ///< The user cancelled the underlying substitution (Ctrl+C).
    TimedOut, ///< The underlying substitution exceeded its time budget.
};

/// @brief Result of executing a completer function, including any compilation errors.
struct CompleterExecutionResult
{
    std::vector<CollectedCompletion> completions;                   ///< Completion candidates.
    std::vector<std::string> errors;                                ///< Formatted diagnostics (if any).
    CompleterExecutionStatus status = CompleterExecutionStatus::Ok; ///< Termination status.
};

/// @brief Callback type for executing an endo completer function.
///
/// @param funcName The function name to invoke (e.g., "flatpak_complete").
/// @param args Tokens after the command name, excluding the current word.
/// @param prefix The current word being typed (may be empty).
/// @return Completions and any compilation errors.
using CompleterExecutionCallback = std::function<CompleterExecutionResult(
    std::string_view funcName, std::vector<std::string> const& args, std::string_view prefix)>;

// Persistent cross-session cache (L2). Forward-declared to avoid a circular include
// (CompletionCache.hpp needs CollectedCompletion from this header).
class CompletionCache;

/// @brief Tunables for the scripted completer's caching and staleness policy.
///
/// Data-driven so the numbers live in one place and can be overridden from
/// `init.endo` (see the `shell_completion_*` properties). Times are wall-clock
/// because the on-disk cache (L2) must survive process restarts.
struct ScriptedCompleterConfig
{
    /// Within this age a cached entry is served as-is, with no refetch. Matches
    /// fish's `rpm -qa` cache window.
    std::chrono::seconds freshTtl { 250 };

    /// Beyond @ref freshTtl but within this ceiling, a cached entry is still usable
    /// (served if a refresh is unavailable/fails) but a refresh is attempted first.
    /// Beyond this the entry is discarded and a cold fetch is forced. Bounds the
    /// damage from wall-clock skew.
    std::chrono::seconds hardTtl { std::chrono::hours { 6 } };

    /// When false, the on-disk (L2) layer is bypassed entirely (in-memory L1 only).
    bool persistentCacheEnabled = true;
};

/// @brief Completion provider that delegates to endo-scripted completer functions.
///
/// Looks up the command in the CompleterFunctionRegistry, calls the registered
/// function via an execution callback, and converts results to scored
/// CompletionItems.
///
/// Caching is two-tier and stale-while-revalidate:
///  - L1: in-process map keyed by (functionName, args, optionPrefix) — the key
///    deliberately EXCLUDES the typed prefix, so once a package list is cached every
///    subsequent keystroke filters it client-side (via fuzzy scoring) for free.
///  - L2: optional persistent @ref CompletionCache, so the list survives restarts.
///
/// For an @c Autosuggest (ghost-text) request the completer is cache-only: it never
/// invokes the callback (which may shell out to `$(dnf repoquery)` / `$(rpm -qa)`),
/// so typing never blocks on a subprocess. Only an explicit Tab may fetch.
class ScriptedCompleter: public CompletionProvider
{
  public:
    /// @brief Constructs a scripted completer.
    /// @param registry The function registry mapping commands to function names.
    /// @param callback The execution callback for invoking endo functions.
    /// @param persistentCache Optional persistent (L2) cache; nullptr → in-memory only.
    /// @param config Caching/staleness tunables.
    /// @param clock Wall-clock source (injectable for tests); default system_clock::now.
    ScriptedCompleter(CompleterFunctionRegistry const& registry,
                      CompleterExecutionCallback callback,
                      CompletionCache const* persistentCache = nullptr,
                      ScriptedCompleterConfig config = {},
                      std::function<std::chrono::system_clock::time_point()> clock = {});

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
    CompletionCache const* _persistentCache;
    ScriptedCompleterConfig _config;
    std::function<std::chrono::system_clock::time_point()> _clock;

    /// One in-memory (L1) cache entry. Wall-clock timestamp so it shares one freshness
    /// rule with the persistent (L2) layer — no steady/wall clock mismatch.
    struct CacheEntry
    {
        std::vector<CollectedCompletion> results;
        std::chrono::system_clock::time_point timestamp;
    };

    static constexpr size_t MaxCacheEntries = 32;
    mutable std::unordered_map<std::string, CacheEntry> _cache;

    /// @brief Looks up @p key in L1, falling back to L2 (populating L1 on an L2 hit).
    /// @return A pointer into the L1 map (never copies the entry), or nullptr on a miss.
    ///         Valid for the duration of the enclosing @ref complete call (single-threaded;
    ///         entries are only appended/reassigned).
    [[nodiscard]] CacheEntry const* lookup(std::string const& key) const;

    /// @brief Stores @p results under @p key in both L1 and L2 with the current time.
    /// @return A reference to the stored L1 entry (so the caller can score it without a
    ///         second lookup or a copy).
    [[nodiscard]] CacheEntry const& storeResult(std::string const& key,
                                                std::vector<CollectedCompletion> results) const;

    /// @brief Scores @p results against @p prefix and converts to CompletionItems.
    [[nodiscard]] static std::vector<CompletionItem> scored(std::vector<CollectedCompletion> const& results,
                                                            std::string_view prefix);

    /// @brief Builds a cache key from function name and args.
    [[nodiscard]] static std::string makeCacheKey(std::string_view funcName,
                                                  std::vector<std::string> const& args,
                                                  bool optionPrefix);

    /// @brief Extracts argument tokens from the full input, excluding command and current word.
    [[nodiscard]] static std::vector<std::string> extractArgs(std::string_view fullInput,
                                                              std::string_view command,
                                                              std::string_view prefix);

    std::vector<std::string> _lastErrors; ///< Errors from the most recent completer execution.
};

} // namespace endo
