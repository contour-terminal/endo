// SPDX-License-Identifier: Apache-2.0
#include <shell/completion/CompletionAdapter.hpp>
#include <shell/completion/CompletionCache.hpp>
#include <shell/completion/ScriptedCompleter.hpp>

#include <endo-language/ide/CompletionContext.hpp>

#include <sstream>
#include <utility>

namespace endo
{

namespace
{
    /// Base fuzzy-scoring weight for scripted (enumerated data) completions.
    constexpr int ScriptedBaseScore = 60;

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

    // Evict expired L1 entries when the map grows past its cap (keeps it bounded
    // without a full LRU: anything older than hardTtl is dead weight anyway).
    if (_cache.size() > MaxCacheEntries)
        std::erase_if(_cache, [&](auto const& e) { return now - e.second.timestamp >= _config.hardTtl; });

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
    auto const withinHardTtl = cached && (now - cached->timestamp) < _config.hardTtl;
    auto const isFresh = cached && (now - cached->timestamp) < _config.freshTtl;

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
    // an empty result when we already have data.
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
