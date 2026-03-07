// SPDX-License-Identifier: Apache-2.0
#include "TypeDefinitionProvider.hpp"

#include "SymbolCollector.hpp"

namespace endo::lsp
{

std::optional<Location> computeTypeDefinition(std::string const& source,
                                              std::string const& uri,
                                              Position position)
{
    auto table = collectSymbols(source);
    if (!table)
        return std::nullopt;

    // Find the symbol at the cursor position
    auto const symbolRange = findSymbolRangeAt(source, position);
    if (!symbolRange)
        return std::nullopt;

    // Find the definition for the symbol at cursor
    std::string typeName;
    for (auto const& def: table->definitions)
    {
        if (def.location.begin.line == symbolRange->begin.line
            && def.location.begin.column == symbolRange->begin.column)
        {
            // Check for type annotation in detail string
            if (def.detail.has_value())
            {
                // Detail format is typically "type: TypeName" or just "TypeName"
                auto detail = *def.detail;
                auto const colonPos = detail.find(':');
                if (colonPos != std::string::npos)
                    typeName = detail.substr(colonPos + 2);
                else
                    typeName = detail;
            }
            break;
        }

        // Check if the cursor is on a reference that resolves to a typed definition
        for (auto const& ref: table->references)
        {
            if (ref.location.begin.line == symbolRange->begin.line
                && ref.location.begin.column == symbolRange->begin.column && ref.definitionIndex >= 0)
            {
                auto const& refDef = table->definitions[static_cast<size_t>(ref.definitionIndex)];
                if (refDef.detail.has_value())
                {
                    auto detail = *refDef.detail;
                    auto const colonPos = detail.find(':');
                    if (colonPos != std::string::npos)
                        typeName = detail.substr(colonPos + 2);
                    else
                        typeName = detail;
                }
                break;
            }
        }
    }

    if (typeName.empty())
        return std::nullopt;

    // Find the type definition (RecordType or UnionType)
    for (auto const& def: table->definitions)
    {
        if (def.name == typeName
            && (def.category == SymbolCategory::RecordType || def.category == SymbolCategory::UnionType))
        {
            return Location {
                .uri = uri,
                .range = toRange(def.location),
            };
        }
    }

    return std::nullopt;
}

} // namespace endo::lsp
