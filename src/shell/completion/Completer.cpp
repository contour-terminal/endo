// SPDX-License-Identifier: Apache-2.0
#include "Completer.hpp"
#include <shell/completion/BuiltinSpecs.hpp>
#include <shell/completion/DirconfigSpec.hpp>
#include <shell/completion/GitSpec.hpp>
#include <shell/completion/HistorySpec.hpp>
#include <shell/completion/ScriptedCompleter.hpp>

#include <tui/completer/Completer.hpp>

#include <algorithm>
#include <ranges>
#include <unordered_set>

namespace endo
{

Completer::Completer(EnvironmentProvider const& env,
                     History const& history,
                     FSharpPersistentState const& fsharpState)
{
    // Register default providers in priority order
    _providers.push_back(std::make_unique<BuiltinArgumentCompleter>());
    _providers.push_back(std::make_unique<CommandCompleter>(env, history));
    _providers.push_back(std::make_unique<FSharpCompleter>(fsharpState));

    // Git command spec completer (cmake/ssh/scp/ctest moved to scripted completers)
    auto specCompleter = std::make_unique<CommandSpecCompleter>();
    specCompleter->registerCommand(createGitSpec(), std::make_unique<GitQueryProvider>());
    specCompleter->registerCommand(createDirconfigSpec(), nullptr);
    for (auto& spec: createBuiltinSpecs())
        specCompleter->registerCommand(std::move(spec), nullptr);
    // Register after createBuiltinSpecs() to overwrite the auto-generated spec with subcommands
    specCompleter->registerCommand(createHistorySpec(), nullptr);
    _providers.push_back(std::move(specCompleter));

    _providers.push_back(std::make_unique<LetBindingCompleter>(fsharpState));
    _providers.push_back(std::make_unique<VariableCompleter>(env));
    _providers.push_back(std::make_unique<FileCompleter>());
    _providers.push_back(std::make_unique<HistoryCompleter>(history));

    // Sort by priority (highest first)
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    std::ranges::sort(_providers, [](auto const& a, auto const& b) { return a->priority() > b->priority(); });
}

void Completer::addProvider(std::unique_ptr<CompletionProvider> provider)
{
    _providers.push_back(std::move(provider));

    // Re-sort by priority
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    std::ranges::sort(_providers, [](auto const& a, auto const& b) { return a->priority() > b->priority(); });
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
    if (input.empty())
        return std::nullopt;

    auto const ctx = analyzeContext(input, cursorPosition);

    // Phase 1: Full-line prefix matching (fish-style).
    // Query Command-capable providers for entries matching the entire input line.
    // History entries are complete command lines and naturally match here.
    // Iterate lowest-priority first so history is preferred over short completions.
    for (auto const& provider: _providers | std::views::reverse)
    {
        if (!provider->canHandle(CompletionContextType::Command))
            continue;

        auto const completions = provider->complete(ctx);
        for (auto const& item: completions)
        {
            if (item.text.starts_with(input) && item.text.size() > input.size())
                return item.text.substr(input.size());
        }
    }

    // Phase 2: Word-level prefix matching from context-appropriate providers.
    if (!ctx.prefix.empty())
    {
        auto const completions = gatherCompletions(ctx);
        for (auto const& item: completions)
        {
            if (item.text.starts_with(ctx.prefix) && item.text.size() > ctx.prefix.size())
                return item.text.substr(ctx.prefix.size());
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

CompletionContext Completer::analyzeContext(std::string_view input, size_t cursorPosition)
{
    return CompletionContextAnalyzer::analyze(input, cursorPosition);
}

std::vector<CompletionItem> Completer::gatherCompletions(CompletionContext const& ctx) const
{
    std::vector<CompletionItem> allResults;
    std::unordered_set<std::string> seenTexts;

    for (auto const& provider: _providers)
    {
        if (!provider->canHandle(ctx.type))
            continue;

        auto results = provider->complete(ctx);

        // If this provider claims exclusivity and returned results, discard all
        // previously accumulated results from higher-priority non-exclusive providers
        // and use only this provider's results.
        if (!results.empty() && provider->isExclusiveFor(ctx))
        {
            seenTexts.clear();
            for (auto const& item: results)
                seenTexts.insert(item.text);
            allResults = std::move(results);
            break;
        }

        for (auto& item: results)
        {
            if (auto [_, inserted] = seenTexts.insert(item.text); !inserted)
                continue;
            allResults.push_back(std::move(item));
        }
    }

    // Sort by score (descending), then alphabetically
    std::ranges::sort(allResults, [](auto const& a, auto const& b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.text < b.text;
    });

    return allResults;
}

std::vector<std::string> Completer::takeLastErrors()
{
    for (auto& provider: _providers)
    {
        if (auto* scripted = dynamic_cast<ScriptedCompleter*>(provider.get()))
        {
            auto errors = scripted->takeLastErrors();
            if (!errors.empty())
                return errors;
        }
    }
    return {};
}

std::string Completer::findCommonPrefix(std::vector<CompletionItem> const& items)
{
    return tui::Completer::findCommonPrefix(items);
}

} // namespace endo
