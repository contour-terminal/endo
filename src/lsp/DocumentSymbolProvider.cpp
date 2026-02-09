// SPDX-License-Identifier: Apache-2.0
#include "DocumentSymbolProvider.hpp"

#include "SymbolCollector.hpp"

namespace endo::lsp
{

std::vector<DocumentSymbol> computeDocumentSymbols(std::string const& source)
{
    auto table = collectSymbols(source);
    if (!table)
        return {};

    std::vector<DocumentSymbol> result;

    for (auto const& def: table->definitions)
    {
        // Only top-level symbols: not parameters, not enclosed in a function, at nesting depth 0
        if (def.isParameter || def.enclosingFunction.has_value() || def.nestingDepth > 0)
            continue;

        auto const nameRange = toRange(def.location);

        auto symbol = DocumentSymbol {
            .name = def.name,
            .kind = def.isFunction ? SymbolKind::Function : SymbolKind::Variable,
            .range = nameRange,
            .selectionRange = nameRange,
        };

        // Add parameters as children for function definitions
        if (def.isFunction)
        {
            for (auto const& childDef: table->definitions)
            {
                if (childDef.isParameter && childDef.enclosingFunction == def.name)
                {
                    auto const childRange = toRange(childDef.location);
                    symbol.children.push_back(DocumentSymbol {
                        .name = childDef.name,
                        .kind = SymbolKind::Variable,
                        .range = childRange,
                        .selectionRange = childRange,
                    });
                }
            }
        }

        result.push_back(std::move(symbol));
    }

    return result;
}

} // namespace endo::lsp
