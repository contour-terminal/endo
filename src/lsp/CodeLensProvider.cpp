// SPDX-License-Identifier: Apache-2.0
#include "CodeLensProvider.hpp"

#include <format>

#include "SymbolCollector.hpp"

namespace endo::lsp
{

std::vector<CodeLens> computeCodeLenses(std::string const& source, std::string const& uri)
{
    auto table = collectSymbols(source);
    if (!table)
        return {};

    std::vector<CodeLens> result;

    for (auto const& def: table->definitions)
    {
        if (def.category != SymbolCategory::Function)
            continue;
        // Skip nested functions
        if (def.enclosingSymbol.has_value() || def.nestingDepth > 0)
            continue;

        auto const range = toRange(def.location);
        result.push_back(CodeLens {
            .range = range,
            .data =
                nlohmann::json {
                    { "name", def.name },
                    { "uri", uri },
                },
        });
    }

    return result;
}

CodeLens resolveCodeLens(std::string const& source, CodeLens lens)
{
    auto refCount = 0;

    if (lens.data.has_value() && lens.data->contains("name"))
    {
        auto const name = lens.data->at("name").get<std::string>();

        auto table = collectSymbols(source);
        if (table)
        {
            // Count references to this function (excluding the definition itself)
            for (auto const& ref: table->references)
            {
                if (ref.name == name && ref.definitionIndex >= 0)
                {
                    auto const& def = table->definitions[static_cast<size_t>(ref.definitionIndex)];
                    if (def.name == name && def.category == SymbolCategory::Function)
                        ++refCount;
                }
            }
        }
    }

    lens.command = LspCommand {
        .title = std::format("{} reference{}", refCount, refCount == 1 ? "" : "s"),
        .command = "endo.showReferences",
    };

    return lens;
}

} // namespace endo::lsp
