// SPDX-License-Identifier: Apache-2.0
#include "BuiltinArgumentCompleter.hpp"
#include <shell/CompletionAdapter.hpp>

#include <endo-language/CompletionCandidates.hpp>

namespace endo
{

std::vector<CompletionItem> BuiltinArgumentCompleter::complete(CompletionContext const& context)
{
    if (!context.command.has_value() || !isBuiltinWithArgumentCompletion(*context.command))
        return {};

    auto candidates = builtinArgumentCandidates(*context.command, context.prefix);
    return applyFuzzyScoring(candidates, context.prefix, 80);
}

bool BuiltinArgumentCompleter::canHandle(CompletionContextType type) const
{
    return type == CompletionContextType::Argument;
}

} // namespace endo
