// SPDX-License-Identifier: Apache-2.0
#include "DefinitionProvider.hpp"

#include "SymbolCollector.hpp"

namespace endo::lsp
{

std::optional<Location> computeDefinition(std::string const& source,
                                          std::string const& uri,
                                          Position position)
{
    auto defLocation = findDefinition(source, position);
    if (!defLocation)
        return std::nullopt;

    return Location {
        .uri = uri,
        .range = toRange(*defLocation),
    };
}

} // namespace endo::lsp
