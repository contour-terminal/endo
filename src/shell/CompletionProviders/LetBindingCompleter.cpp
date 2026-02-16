// SPDX-License-Identifier: Apache-2.0
#include "LetBindingCompleter.hpp"
#include <shell/CompletionAdapter.hpp>

#include <endo-language/CompletionCandidates.hpp>
#include <endo-language/CompletionItem.hpp>
#include <endo-language/Type.hpp>

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
            info.isRecursive = func.isRecursive;
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
    std::sort(results.begin(), results.end(), [](auto const& a, auto const& b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.text < b.text;
    });

    return results;
}

bool LetBindingCompleter::canHandle(CompletionContextType type) const
{
    return type == CompletionContextType::Command;
}

std::string LetBindingCompleter::formatFunctionDescription(
    std::string const& name, FSharpPersistentState::PersistedFunction const& func)
{
    std::string result;

    if (func.isRecursive)
        result += "rec ";

    result += name;
    result += '(';

    for (size_t i = 0; i < func.parameters.size(); ++i)
    {
        if (i > 0)
            result += ", ";

        result += func.parameters[i];

        if (i < func.parameterTypes.size() && func.parameterTypes[i].has_value())
        {
            result += ": ";
            result += toString(*func.parameterTypes[i]);
        }
    }

    result += ')';

    if (func.returnType.has_value())
    {
        result += " -> ";
        result += toString(*func.returnType);
    }

    return result;
}

std::string LetBindingCompleter::formatValueDescription(
    FSharpPersistentState::PersistedValueBinding const& binding)
{
    return binding.isMutable ? "mutable value" : "value";
}

} // namespace endo
