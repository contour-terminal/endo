// SPDX-License-Identifier: Apache-2.0
#include "CompletionProvider.hpp"

#include "SymbolCollector.hpp"
#include <endo-language/Completer.hpp>
#include <endo-language/CompletionItem.hpp>

namespace endo::lsp
{

namespace
{
    /// @brief Converts a byte offset in source to a Position (line, character).
    [[nodiscard]] size_t positionToByteOffset(std::string const& source, Position position)
    {
        size_t offset = 0;
        auto line = 0;

        for (size_t i = 0; i < source.size(); ++i)
        {
            if (line == position.line)
            {
                offset = i + static_cast<size_t>(position.character);
                break;
            }
            if (source[i] == '\n')
                ++line;
        }

        // Clamp to source size
        if (offset > source.size())
            offset = source.size();

        return offset;
    }

    /// @brief Converts CompletionKind to LSP CompletionItemKind.
    [[nodiscard]] CompletionItemKind toCompletionItemKind(endo::CompletionKind kind)
    {
        switch (kind)
        {
            case endo::CompletionKind::Keyword: return CompletionItemKind::Keyword;
            case endo::CompletionKind::Function: return CompletionItemKind::Function;
            case endo::CompletionKind::Variable: return CompletionItemKind::Variable;
            case endo::CompletionKind::Constructor: return CompletionItemKind::Constructor;
            case endo::CompletionKind::Module: return CompletionItemKind::Module;
            case endo::CompletionKind::Field: return CompletionItemKind::Field;
            case endo::CompletionKind::Builtin: return CompletionItemKind::Function;
            case endo::CompletionKind::Command: return CompletionItemKind::Text;
            case endo::CompletionKind::EnumValue: return CompletionItemKind::EnumMember;
            case endo::CompletionKind::Other: return CompletionItemKind::Text;
        }
        return CompletionItemKind::Text;
    }

    /// @brief Converts LSP SymbolCollector definitions to shared SymbolDefinitionInfo.
    [[nodiscard]] std::vector<endo::SymbolDefinitionInfo> convertSymbols(SymbolTable const& table)
    {
        std::vector<endo::SymbolDefinitionInfo> result;

        for (auto const& def: table.definitions)
        {
            // Skip parameters — we only want top-level symbols for completion
            if (def.isParameter)
                continue;

            endo::SymbolDefinitionInfo info;
            info.name = def.name;
            info.isFunction = def.isFunction;
            info.parameterNames = def.parameterNames;

            for (auto const& pt: def.parameterTypes)
                info.parameterTypes.push_back(pt.empty() ? std::nullopt : std::optional(pt));

            if (def.returnType.has_value())
                info.returnType = *def.returnType;

            result.push_back(std::move(info));
        }

        return result;
    }
} // namespace

nlohmann::json computeCompletion(std::string const& source, Position position)
{
    auto const byteOffset = positionToByteOffset(source, position);

    // Build data source from document analysis
    endo::CompletionDataSource dataSource;

    // Collect symbols from the document
    auto symbolTable = collectSymbols(source);
    if (symbolTable.has_value())
        dataSource.symbols = convertSymbols(*symbolTable);

    // Collect record type info for type-aware dot-access completion
    auto recordInfo = endo::collectRecordInfo(source);
    dataSource.recordFields = std::move(recordInfo.recordFields);
    dataSource.variableTypes = std::move(recordInfo.variableTypes);

    // Compute completions using shared engine
    auto candidates = endo::computeCompletions(source, byteOffset, dataSource);

    // Convert to LSP CompletionItem JSON
    auto result = nlohmann::json::array();
    for (auto const& candidate: candidates)
    {
        LspCompletionItem item;
        item.label = candidate.text;
        item.kind = toCompletionItemKind(candidate.kind);
        item.detail = candidate.description;
        if (!candidate.detail.empty())
            item.documentation = candidate.detail;

        result.push_back(item);
    }

    return result;
}

} // namespace endo::lsp
