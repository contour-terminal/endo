// SPDX-License-Identifier: Apache-2.0
#include "WorkspaceSymbolProvider.hpp"

#include <algorithm>
#include <cctype>

#include "SymbolCollector.hpp"

namespace endo::lsp
{

namespace
{
    /// Case-insensitive substring match.
    [[nodiscard]] bool containsIgnoreCase(std::string const& haystack, std::string const& needle)
    {
        if (needle.empty())
            return true;
        auto it = std::ranges::search(haystack, needle, [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
        });
        return !it.empty();
    }
} // namespace

std::vector<SymbolInformation> computeWorkspaceSymbols(endo::editor_protocol::DocumentStore const& documents,
                                                       std::string const& query)
{
    std::vector<SymbolInformation> result;

    for (auto const& uri: documents.uris())
    {
        auto const* source = documents.get(uri);
        if (!source)
            continue;

        auto table = collectSymbols(*source);
        if (!table)
            continue;

        for (auto const& def: table->definitions)
        {
            // Skip parameters and child symbols
            if (def.category == SymbolCategory::Parameter)
                continue;
            if (def.category == SymbolCategory::RecordField || def.category == SymbolCategory::UnionVariant)
                continue;

            if (!containsIgnoreCase(def.name, query))
                continue;

            result.push_back(SymbolInformation {
                .name = def.name,
                .kind = categoryToSymbolKind(def.category),
                .location =
                    Location {
                        .uri = uri,
                        .range = toRange(def.location),
                    },
                .containerName = def.enclosingSymbol,
            });
        }
    }

    return result;
}

} // namespace endo::lsp
