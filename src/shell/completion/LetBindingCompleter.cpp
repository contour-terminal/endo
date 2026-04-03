// SPDX-License-Identifier: Apache-2.0
#include "LetBindingCompleter.hpp"
#include <shell/completion/CompletionAdapter.hpp>

#include <endo-language/ide/CompletionCandidates.hpp>
#include <endo-language/ide/CompletionItem.hpp>
#include <endo-language/types/Type.hpp>

#include <algorithm>

namespace endo
{

namespace
{
    /// @brief Converts FSharpPersistentState to SymbolDefinitionInfo for the shared engine.
    [[nodiscard]] std::vector<SymbolDefinitionInfo> convertToSymbolInfo(FSharpPersistentState const& state)
    {
        std::vector<SymbolDefinitionInfo> symbols;

        for (auto const& [name, func]: state.functions)
        {
            SymbolDefinitionInfo info;
            info.name = name;
            info.isFunction = true;
            info.parameterNames = func.parameters;
            info.isRecursive = func.recursion == ast::Recursion::Recursive;
            for (auto const& pt: func.parameterTypes)
                info.parameterTypes.push_back(pt.has_value() ? std::optional(toString(*pt)) : std::nullopt);
            if (func.returnType.has_value())
                info.returnType = toString(*func.returnType);
            symbols.push_back(std::move(info));
        }

        for (auto const& binding: state.valueBindings)
        {
            SymbolDefinitionInfo info;
            info.name = binding.name;
            info.isFunction = false;
            info.isMutable = binding.isMutable;
            symbols.push_back(std::move(info));
        }

        return symbols;
    }
} // namespace

LetBindingCompleter::LetBindingCompleter(FSharpPersistentState const& state): _state(state)
{
}

std::vector<CompletionItem> LetBindingCompleter::complete(CompletionContext const& context)
{
    // Skip symbol suggestions for builtins that accept only enumerated values
    if (context.command.has_value() && isBuiltinWithArgumentCompletion(*context.command))
        return {};

    auto const& prefix = context.prefix;

    // Convert persisted state to shared symbol format
    auto const symbols = convertToSymbolInfo(_state);

    // Generate candidates using shared engine
    auto candidates = symbolCandidates(symbols);

    // Apply fuzzy scoring for user symbols (higher base score)
    auto results = applyFuzzyScoring(candidates, prefix, 80);

    // Add type constructor candidates (Some, None, Ok, Error, etc.)
    auto ctorCandidates = constructorCandidates();
    auto ctorResults = applyFuzzyScoring(ctorCandidates, prefix, 70);
    for (auto& item: ctorResults)
    {
        auto const isDuplicate =
            std::ranges::any_of(results, [&](auto const& existing) { return existing.text == item.text; });
        if (!isDuplicate)
            results.push_back(std::move(item));
    }

    // Add standard library candidates at slightly lower base score
    // so user-defined functions rank higher
    auto stdlibCandidates = standardLibraryCandidates();
    auto stdlibResults = applyFuzzyScoring(stdlibCandidates, prefix, 75);

    // Deduplicate: user-defined symbols shadow stdlib
    for (auto& item: stdlibResults)
    {
        auto const isDuplicate =
            std::ranges::any_of(results, [&](auto const& existing) { return existing.text == item.text; });
        if (!isDuplicate)
            results.push_back(std::move(item));
    }

    // Sort by score (descending), then alphabetically
    std::ranges::sort(results, [](auto const& a, auto const& b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.text < b.text;
    });

    return results;
}

bool LetBindingCompleter::canHandle(CompletionContextType type) const
{
    return type == CompletionContextType::Command || type == CompletionContextType::Argument;
}

} // namespace endo
