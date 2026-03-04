// SPDX-License-Identifier: Apache-2.0
#include "DocumentHighlightProvider.hpp"

#include "SymbolCollector.hpp"

namespace endo::lsp
{

std::vector<DocumentHighlight> computeDocumentHighlights(std::string const& source, Position position)
{
    auto entries = findHighlights(source, position);

    std::vector<DocumentHighlight> result;
    result.reserve(entries.size());
    for (auto const& entry: entries)
    {
        result.push_back(DocumentHighlight {
            .range = toRange(entry.range),
            .kind = entry.isDefinition ? DocumentHighlightKind::Write : DocumentHighlightKind::Read,
        });
    }
    return result;
}

} // namespace endo::lsp
