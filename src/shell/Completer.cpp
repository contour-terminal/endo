// SPDX-License-Identifier: Apache-2.0
#include "Completer.hpp"

#include <algorithm>

namespace endo
{

Completer::Completer(Environment const& env, History const& history, FSharpPersistentState const& fsharpState)
{
    // Register default providers in priority order
    _providers.push_back(std::make_unique<CommandCompleter>(env));
    _providers.push_back(std::make_unique<LetBindingCompleter>(fsharpState));
    _providers.push_back(std::make_unique<VariableCompleter>(env));
    _providers.push_back(std::make_unique<OptionCompleter>());
    _providers.push_back(std::make_unique<FileCompleter>());
    _providers.push_back(std::make_unique<HistoryCompleter>(history));

    // Sort by priority (highest first)
    std::sort(_providers.begin(), _providers.end(), [](auto const& a, auto const& b) {
        return a->priority() > b->priority();
    });
}

void Completer::addProvider(std::unique_ptr<CompletionProvider> provider)
{
    _providers.push_back(std::move(provider));

    // Re-sort by priority
    std::sort(_providers.begin(), _providers.end(), [](auto const& a, auto const& b) {
        return a->priority() > b->priority();
    });
}

std::vector<CompletionItem> Completer::complete(std::string_view input, size_t cursorPosition) const
{
    auto ctx = analyzeContext(input, cursorPosition);
    auto results = gatherCompletions(ctx);

    // Limit results
    if (results.size() > _config.maxSuggestions)
        results.resize(_config.maxSuggestions);

    return results;
}

std::optional<std::string> Completer::suggest(std::string_view input, size_t cursorPosition) const
{
    // Don't show ghost text when input is empty
    if (input.empty())
        return std::nullopt;

    // Fish-style ghost text: only show suggestions for command context
    // This prevents ghost text from appearing after string literals, arguments, etc.
    // Users can still use Tab to trigger the completion popup in any context.

    auto ctx = analyzeContext(input, cursorPosition);

    // Ghost text is only shown for command context (fish-style behavior)
    if (ctx.type != CompletionContextType::Command)
        return std::nullopt;

    // Check providers for history-based command suggestions
    for (auto const& provider: _providers)
    {
        if (!provider->canHandle(ctx.type))
            continue;

        auto completions = provider->complete(ctx);
        if (completions.empty())
            continue;

        // Find a completion that starts with the full input
        for (auto const& item: completions)
        {
            if (item.text.starts_with(input) && item.text.size() > input.size())
                return item.text.substr(input.size());
        }
    }

    return std::nullopt;
}

void Completer::setConfig(CompletionConfig config)
{
    _config = config;
}

CompletionConfig const& Completer::config() const
{
    return _config;
}

CompletionContext Completer::analyzeContext(std::string_view input, size_t cursorPosition) const
{
    return CompletionContextAnalyzer::analyze(input, cursorPosition);
}

std::vector<CompletionItem> Completer::gatherCompletions(CompletionContext const& ctx) const
{
    std::vector<CompletionItem> allResults;

    for (auto const& provider: _providers)
    {
        if (!provider->canHandle(ctx.type))
            continue;

        auto results = provider->complete(ctx);
        for (auto& item: results)
        {
            // Avoid duplicates
            bool isDuplicate = false;
            for (auto const& existing: allResults)
            {
                if (existing.text == item.text)
                {
                    isDuplicate = true;
                    break;
                }
            }

            if (!isDuplicate)
                allResults.push_back(std::move(item));
        }
    }

    // Sort by score (descending), then alphabetically
    std::sort(allResults.begin(), allResults.end(), [](auto const& a, auto const& b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.text < b.text;
    });

    return allResults;
}

std::string Completer::findCommonPrefix(std::vector<CompletionItem> const& items)
{
    if (items.empty())
        return "";

    if (items.size() == 1)
        return items[0].text;

    std::string prefix = items[0].text;

    for (size_t i = 1; i < items.size(); ++i)
    {
        auto const& text = items[i].text;
        size_t j = 0;

        while (j < prefix.size() && j < text.size() && prefix[j] == text[j])
            ++j;

        prefix = prefix.substr(0, j);

        if (prefix.empty())
            break;
    }

    return prefix;
}

} // namespace endo
