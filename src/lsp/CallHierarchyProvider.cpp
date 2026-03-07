// SPDX-License-Identifier: Apache-2.0
#include "CallHierarchyProvider.hpp"

#include <unordered_map>

#include "SymbolCollector.hpp"

namespace endo::lsp
{

namespace
{
    /// Finds the function definition index at the given cursor position.
    [[nodiscard]] int findFunctionDefAt(SymbolTable const& table, Position position)
    {
        for (auto i = 0; std::cmp_less(i, table.definitions.size()); ++i)
        {
            auto const& def = table.definitions[static_cast<size_t>(i)];
            if (def.category != SymbolCategory::Function)
                continue;

            auto const range = toRange(def.location);
            if (position.line == range.start.line && position.character >= range.start.character
                && position.character <= range.end.character)
                return i;
        }
        return -1;
    }

    /// Creates a CallHierarchyItem from a function definition.
    [[nodiscard]] CallHierarchyItem makeItem(SymbolDefinition const& def, std::string const& uri)
    {
        auto const range = toRange(def.location);
        return CallHierarchyItem {
            .name = def.name,
            .kind = SymbolKind::Function,
            .uri = uri,
            .range = range,
            .selectionRange = range,
            .detail = def.detail,
            .data = nlohmann::json { { "name", def.name } },
        };
    }

    /// Finds a function definition by name.
    [[nodiscard]] int findFunctionByName(SymbolTable const& table, std::string const& name)
    {
        for (auto i = 0; std::cmp_less(i, table.definitions.size()); ++i)
        {
            auto const& def = table.definitions[static_cast<size_t>(i)];
            if (def.name == name && def.category == SymbolCategory::Function)
                return i;
        }
        return -1;
    }
} // namespace

std::vector<CallHierarchyItem> prepareCallHierarchy(std::string const& source,
                                                    std::string const& uri,
                                                    Position position)
{
    auto table = collectSymbols(source);
    if (!table)
        return {};

    auto const defIndex = findFunctionDefAt(*table, position);
    if (defIndex < 0)
        return {};

    return { makeItem(table->definitions[static_cast<size_t>(defIndex)], uri) };
}

std::vector<CallHierarchyIncomingCall> computeIncomingCalls(std::string const& source,
                                                            std::string const& uri,
                                                            CallHierarchyItem const& item)
{
    auto table = collectSymbols(source);
    if (!table)
        return {};

    auto const calleeIndex = findFunctionByName(*table, item.name);
    if (calleeIndex < 0)
        return {};

    // Group call relations by caller
    std::unordered_map<int, std::vector<Range>> callerRanges;
    for (auto const& rel: table->callRelations)
    {
        if (rel.calleeDefIndex == calleeIndex)
            callerRanges[rel.callerDefIndex].push_back(toRange(rel.callSite));
    }

    std::vector<CallHierarchyIncomingCall> result;
    for (auto const& [callerIdx, ranges]: callerRanges)
    {
        if (callerIdx < 0)
            continue; // Top-level calls
        auto const& callerDef = table->definitions[static_cast<size_t>(callerIdx)];
        result.push_back(CallHierarchyIncomingCall {
            .from = makeItem(callerDef, uri),
            .fromRanges = ranges,
        });
    }

    return result;
}

std::vector<CallHierarchyOutgoingCall> computeOutgoingCalls(std::string const& source,
                                                            std::string const& uri,
                                                            CallHierarchyItem const& item)
{
    auto table = collectSymbols(source);
    if (!table)
        return {};

    auto const callerIndex = findFunctionByName(*table, item.name);
    if (callerIndex < 0)
        return {};

    // Group call relations by callee
    std::unordered_map<int, std::vector<Range>> calleeRanges;
    for (auto const& rel: table->callRelations)
    {
        if (rel.callerDefIndex == callerIndex)
            calleeRanges[rel.calleeDefIndex].push_back(toRange(rel.callSite));
    }

    std::vector<CallHierarchyOutgoingCall> result;
    for (auto const& [calleeIdx, ranges]: calleeRanges)
    {
        if (calleeIdx < 0)
            continue;
        auto const& calleeDef = table->definitions[static_cast<size_t>(calleeIdx)];
        result.push_back(CallHierarchyOutgoingCall {
            .to = makeItem(calleeDef, uri),
            .fromRanges = ranges,
        });
    }

    return result;
}

} // namespace endo::lsp
