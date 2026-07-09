// SPDX-License-Identifier: Apache-2.0
#include <shell/completion/CompletionAdapter.hpp>
#include <shell/completion/CompletionCache.hpp>
#include <shell/completion/ScriptedCompleter.hpp>

#include <endo-language/ide/CompletionContext.hpp>

#include <algorithm>
#include <chrono>
#include <ranges>
#include <sstream>
#include <utility>

namespace endo
{

namespace
{
    /// Base fuzzy-scoring weight for scripted (enumerated data) completions.
    constexpr int ScriptedBaseScore = 60;

    /// @brief Whether a cache entry of the given age still satisfies @p ttl.
    ///
    /// The timestamps come from a wall clock (@c system_clock, so the on-disk L2 cache
    /// survives restarts), which is NOT monotonic: an NTP correction, a manual `date`, or
    /// an L2 file written by a machine whose clock ran ahead can put @p timestamp in the
    /// future, making a naive `now - timestamp` negative. A negative age would slip under
    /// every TTL and pin a stale list as fresh indefinitely. We therefore treat a
    /// future-stamped entry (negative age) as NOT satisfying any TTL: it forces a
    /// refetch, while the raw age still lets the hard-TTL check keep it as a stale
    /// fallback if that refetch fails.
    ///
    /// @param age The signed `now - timestamp`; may be negative under wall-clock skew.
    /// @param ttl The window the entry must fall within.
    /// @return True iff @p age is in the range [0, ttl).
    bool withinTtl(std::chrono::system_clock::duration age, std::chrono::system_clock::duration ttl)
    {
        return age >= std::chrono::system_clock::duration::zero() && age < ttl;
    }

    /// Converts cached completions into fuzzy-scoring candidates.
    std::vector<CompletionCandidate> toCandidates(std::vector<CollectedCompletion> const& results)
    {
        std::vector<CompletionCandidate> candidates;
        candidates.reserve(results.size());
        for (auto const& entry: results)
            candidates.push_back({ .text = entry.text,
                                   .description = entry.description,
                                   .detail = entry.detail,
                                   .kind = CompletionKind::EnumValue });
        return candidates;
    }
} // namespace

ScriptedCompleter::ScriptedCompleter(CompleterFunctionRegistry const& registry,
                                     CompleterExecutionCallback callback,
                                     CompletionCache const* persistentCache,
                                     ScriptedCompleterConfig config,
                                     std::function<std::chrono::system_clock::time_point()> clock):
    _registry(registry),
    _callback(std::move(callback)),
    _persistentCache(persistentCache),
    _config(config),
    _clock(clock ? std::move(clock) : [] { return std::chrono::system_clock::now(); })
{
}

bool ScriptedCompleter::canHandle(CompletionContextType type) const
{
    return type == CompletionContextType::Argument || type == CompletionContextType::Option;
}

bool ScriptedCompleter::isExclusiveFor(CompletionContext const& context) const
{
    if (!context.command.has_value())
        return false;
    return _registry.hasCommand(*context.command);
}

std::vector<CompletionItem> ScriptedCompleter::scored(std::vector<CollectedCompletion> const& results,
                                                      std::string_view prefix)
{
    return applyFuzzyScoring(toCandidates(results), prefix, ScriptedBaseScore);
}

ScriptedCompleter::CacheEntry const* ScriptedCompleter::lookup(std::string const& key) const
{
    if (auto const it = _cache.find(key); it != _cache.end())
        return &it->second;

    // L1 miss: consult the persistent (L2) cache and promote a hit into L1.
    if (_persistentCache && _config.persistentCacheEnabled)
    {
        if (auto loaded = _persistentCache->load(key))
        {
            auto const [it, _] = _cache.insert_or_assign(
                key, CacheEntry { .results = std::move(loaded->results), .timestamp = loaded->timestamp });
            return &it->second;
        }
    }
    return nullptr;
}

ScriptedCompleter::CacheEntry const& ScriptedCompleter::storeResult(
    std::string const& key, std::vector<CollectedCompletion> results) const
{
    auto const now = _clock();

    // Keep the L1 map bounded. First drop anything past the hard TTL (dead weight). If
    // that still leaves us over the cap — a burst of distinct completions all younger
    // than hardTtl (6h) — evict the oldest entries by timestamp until we fit, so the map
    // can never grow without bound (each entry can hold a hundreds-of-KB package list).
    if (_cache.size() >= MaxCacheEntries)
    {
        std::erase_if(_cache,
                      [&](auto const& e) { return !withinTtl(now - e.second.timestamp, _config.hardTtl); });

        while (_cache.size() >= MaxCacheEntries)
        {
            auto const oldest =
                std::ranges::min_element(_cache, {}, [](auto const& e) { return e.second.timestamp; });
            if (oldest == _cache.end())
                break;
            _cache.erase(oldest);
        }
    }

    auto const [it, _] =
        _cache.insert_or_assign(key, CacheEntry { .results = std::move(results), .timestamp = now });

    // Persist to L2 from the stored L1 copy — store() only reads it to serialize, so no
    // extra owned copy of the (large) list is needed.
    if (_persistentCache && _config.persistentCacheEnabled)
        _persistentCache->store(key, CachedCompletions { .results = it->second.results, .timestamp = now });

    return it->second;
}

std::vector<CompletionItem> ScriptedCompleter::complete(CompletionContext const& context)
{
    if (!context.command.has_value())
        return {};

    auto const funcName = _registry.functionForCommand(*context.command);
    if (!funcName)
        return {};

    auto const args = extractArgs(context.fullInput, *context.command, context.prefix);
    auto const optionPrefix = !context.prefix.empty() && context.prefix[0] == '-';
    auto const cacheKey = makeCacheKey(*funcName, args, optionPrefix);

    auto const now = _clock();
    auto const* const cached = lookup(cacheKey);
    // Clamp the fresh window to the hard ceiling: a misconfigured freshTtl > hardTtl must
    // never let an entry past its staleness ceiling be served as fresh (which would also
    // widen the wall-clock-skew damage hardTtl exists to bound). withinTtl rejects a
    // negative age (future timestamp) so a backward clock jump forces a refetch instead
    // of pinning a stale list as fresh.
    auto const effectiveFreshTtl = std::min(_config.freshTtl, _config.hardTtl);
    auto const age = cached ? now - cached->timestamp : std::chrono::system_clock::duration::zero();
    auto const withinHardTtl = cached && withinTtl(age, _config.hardTtl);
    auto const isFresh = cached && withinTtl(age, effectiveFreshTtl);

    // Ghost text (autosuggestion) must never shell out: it fires ~100ms after every
    // keystroke and the callback may run `$(dnf repoquery)` / `$(rpm -qa)`. Serve any
    // still-usable cached entry; otherwise return nothing and let a Tab warm it.
    if (context.intent == CompletionIntent::Autosuggest)
    {
        if (withinHardTtl)
            return scored(cached->results, context.prefix);
        return {};
    }

    // Explicit Tab, fresh cache: serve as-is, no fetch.
    if (isFresh)
        return scored(cached->results, context.prefix);

    // Explicit Tab, cold or stale-or-expired: fetch. A stale-but-usable entry is our
    // fallback if the fetch yields nothing (aborted/timed out), so we never regress to
    // an empty result when we already have data. Tell the shell whether that fallback
    // exists so it can pick the cold vs. overall fetch budget.
    if (_preFetchHook)
        _preFetchHook(withinHardTtl);
    auto result = _callback(*funcName, args, context.prefix);
    _lastErrors = std::move(result.errors);

    // Do NOT cache an aborted/timed-out run: its (empty) completions are not
    // authoritative and would poison the cache until they expired.
    if (result.status == CompleterExecutionStatus::Ok)
        return scored(storeResult(cacheKey, std::move(result.completions)).results, context.prefix);

    // Fetch failed: prefer the stale cache over nothing.
    if (withinHardTtl)
        return scored(cached->results, context.prefix);

    return scored(result.completions, context.prefix);
}

std::vector<std::string> ScriptedCompleter::takeLastErrors()
{
    return std::exchange(_lastErrors, {});
}

std::string ScriptedCompleter::makeCacheKey(std::string_view funcName,
                                            std::vector<std::string> const& args,
                                            bool optionPrefix)
{
    std::string key(funcName);
    for (auto const& arg: args)
    {
        key += '\0';
        key += arg;
    }
    if (optionPrefix)
    {
        key += '\0';
        key += '-';
    }
    return key;
}

std::vector<std::string> ScriptedCompleter::extractArgs(std::string_view fullInput,
                                                        std::string_view command,
                                                        std::string_view prefix)
{
    std::vector<std::string> args;

    // Find the command in the input
    auto const cmdPos = fullInput.find(command);
    if (cmdPos == std::string_view::npos)
        return args;

    // Get everything after the command
    auto remaining = fullInput.substr(cmdPos + command.size());

    // Tokenize by whitespace, excluding the prefix (last token)
    auto stream = std::istringstream(std::string(remaining));
    std::vector<std::string> tokens;
    std::string token;
    while (stream >> token)
        tokens.push_back(std::move(token));

    // Remove the last token if it matches the prefix (it's the word being typed)
    if (!tokens.empty() && !prefix.empty() && tokens.back() == prefix)
        tokens.pop_back();

    return tokens;
}

} // namespace endo
