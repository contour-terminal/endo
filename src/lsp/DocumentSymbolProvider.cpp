// SPDX-License-Identifier: Apache-2.0
#include "DocumentSymbolProvider.hpp"

#include "SymbolCollector.hpp"

namespace endo::lsp
{

namespace
{

    /// Returns true if the category represents a child symbol (not top-level).
    [[nodiscard]] bool isChildCategory(SymbolCategory category)
    {
        switch (category)
        {
            case SymbolCategory::Parameter:
            case SymbolCategory::RecordField:
            case SymbolCategory::UnionVariant: return true;
            default: return false;
        }
    }

} // namespace

std::vector<DocumentSymbol> computeDocumentSymbols(std::string const& source)
{
    auto table = collectSymbols(source);
    if (!table)
        return {};

    std::vector<DocumentSymbol> result;

    for (auto const& def: table->definitions)
    {
        // Skip child symbols and nested definitions — they are added as children below
        if (isChildCategory(def.category) || def.enclosingSymbol.has_value() || def.nestingDepth > 0)
            continue;

        auto const nameRange = toRange(def.location);

        auto symbol = DocumentSymbol {
            .name = def.name,
            .detail = def.detail,
            .kind = categoryToSymbolKind(def.category),
            .range = nameRange,
            .selectionRange = nameRange,
        };

        // Collect children: any definition whose enclosingSymbol matches this symbol's name
        for (auto const& childDef: table->definitions)
        {
            if (childDef.enclosingSymbol == def.name)
            {
                auto const childRange = toRange(childDef.location);
                symbol.children.push_back(DocumentSymbol {
                    .name = childDef.name,
                    .detail = childDef.detail,
                    .kind = categoryToSymbolKind(childDef.category),
                    .range = childRange,
                    .selectionRange = childRange,
                });
            }
        }

        result.push_back(std::move(symbol));
    }

    return result;
}

} // namespace endo::lsp
