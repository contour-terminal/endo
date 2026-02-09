// SPDX-License-Identifier: Apache-2.0
#include "ReferencesProvider.hpp"

#include "SymbolCollector.hpp"

namespace endo::lsp
{

std::vector<Location> computeReferences(std::string const& source,
                                        std::string const& uri,
                                        Position position,
                                        bool includeDeclaration)
{
    auto refLocations = findReferences(source, position, includeDeclaration);

    std::vector<Location> result;
    result.reserve(refLocations.size());
    for (auto const& loc: refLocations)
    {
        result.push_back(Location {
            .uri = uri,
            .range = toRange(loc),
        });
    }
    return result;
}

} // namespace endo::lsp
