// SPDX-License-Identifier: Apache-2.0
#include "InlineValueProvider.hpp"

#include "SymbolCollector.hpp"

namespace endo::lsp
{

std::vector<InlineValueVariableLookup> computeInlineValues(std::string const& source, Range range)
{
    auto table = collectSymbols(source);
    if (!table)
        return {};

    std::vector<InlineValueVariableLookup> result;

    for (auto const& ref: table->references)
    {
        auto const refRange = toRange(ref.location);

        // Check if reference is within the requested range
        if (refRange.start.line < range.start.line || refRange.end.line > range.end.line)
            continue;
        if (refRange.start.line == range.start.line && refRange.start.character < range.start.character)
            continue;
        if (refRange.end.line == range.end.line && refRange.end.character > range.end.character)
            continue;

        // Only include references that resolve to a definition
        if (ref.definitionIndex < 0)
            continue;

        auto const& def = table->definitions[static_cast<size_t>(ref.definitionIndex)];

        // Only include variable and parameter references
        if (def.category != SymbolCategory::Variable && def.category != SymbolCategory::Parameter)
            continue;

        result.push_back(InlineValueVariableLookup {
            .range = refRange,
            .variableName = ref.name,
            .caseSensitiveLookup = true,
        });
    }

    return result;
}

} // namespace endo::lsp
