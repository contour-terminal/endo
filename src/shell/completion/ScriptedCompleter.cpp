// SPDX-License-Identifier: Apache-2.0
#include <shell/completion/CompletionAdapter.hpp>
#include <shell/completion/ScriptedCompleter.hpp>

#include <endo-language/ide/CompletionContext.hpp>

#include <sstream>
#include <utility>

namespace endo
{

ScriptedCompleter::ScriptedCompleter(CompleterFunctionRegistry const& registry,
                                     CompleterExecutionCallback callback):
    _registry(registry), _callback(std::move(callback))
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

    // Check cache
    auto const now = std::chrono::steady_clock::now();
    if (auto it = _cache.find(cacheKey); it != _cache.end())
    {
        if (now - it->second.timestamp < CacheTtl)
        {
            // Reuse cached results with current prefix for fuzzy scoring
            std::vector<CompletionCandidate> candidates;
            candidates.reserve(it->second.results.size());
            for (auto const& entry: it->second.results)
                candidates.push_back({ .text = entry.text,
                                       .description = entry.description,
                                       .detail = entry.detail,
                                       .kind = CompletionKind::EnumValue });
            return applyFuzzyScoring(candidates, context.prefix, 60);
        }
    }

    // Evict expired entries when cache exceeds size threshold
    if (_cache.size() > MaxCacheEntries)
        std::erase_if(_cache, [now](auto const& entry) { return now - entry.second.timestamp >= CacheTtl; });

    // Execute the completer function
    auto result = _callback(*funcName, args, context.prefix);

    // Capture any errors for later display
    _lastErrors = std::move(result.errors);

    // Cache the results (move completions into cache, then reference from there)
    auto& cached = _cache[cacheKey];
    cached = CacheEntry { .results = std::move(result.completions), .timestamp = now };

    // Convert to CompletionCandidates and apply fuzzy scoring
    std::vector<CompletionCandidate> candidates;
    candidates.reserve(cached.results.size());
    for (auto const& entry: cached.results)
        candidates.push_back({ .text = entry.text,
                               .description = entry.description,
                               .detail = entry.detail,
                               .kind = CompletionKind::EnumValue });

    return applyFuzzyScoring(candidates, context.prefix, 60);
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
