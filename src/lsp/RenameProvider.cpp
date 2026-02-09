// SPDX-License-Identifier: Apache-2.0
#include "RenameProvider.hpp"

#include "SymbolCollector.hpp"

namespace endo::lsp
{

std::optional<WorkspaceEdit> computeRename(std::string const& source,
                                           std::string const& uri,
                                           Position position,
                                           std::string const& newName)
{
    auto locations = findReferences(source, position, /*includeDeclaration=*/true);
    if (locations.empty())
        return std::nullopt;

    std::vector<TextEdit> edits;
    edits.reserve(locations.size());
    for (auto const& loc: locations)
    {
        edits.push_back(TextEdit {
            .range = toRange(loc),
            .newText = newName,
        });
    }

    return WorkspaceEdit {
        .changes = { { uri, std::move(edits) } },
    };
}

std::optional<Range> prepareRename(std::string const& source, Position position)
{
    // Check that rename is possible by verifying there are references at this position
    auto locations = findReferences(source, position, /*includeDeclaration=*/true);
    if (locations.empty())
        return std::nullopt;

    // Return the range of the identifier at the cursor
    auto symbolRange = findSymbolRangeAt(source, position);
    if (!symbolRange)
        return std::nullopt;

    return toRange(*symbolRange);
}

} // namespace endo::lsp
