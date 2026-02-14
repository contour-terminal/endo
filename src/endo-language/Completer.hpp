// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <endo-language/CompletionItem.hpp>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace endo
{

/// @brief Input data for completion -- provided by the caller (shell or LSP).
struct CompletionDataSource
{
    std::vector<SymbolDefinitionInfo> symbols; ///< Available symbols in scope.
    std::unordered_map<std::string, std::vector<RecordFieldInfo>>
        recordFields; ///< Record type fields (type name -> field info).
    std::unordered_map<std::string, std::string>
        variableTypes; ///< Variable name -> record type name (for type-specific completion).
    std::vector<CompletionCandidate>
        additionalCandidates; ///< Extra candidates (e.g., PATH commands, env vars, history).
};

/// @brief Record info collected from a document for completion support.
struct DocumentRecordInfo
{
    std::unordered_map<std::string, std::vector<RecordFieldInfo>>
        recordFields; ///< Record type fields (type name -> field info).
    std::unordered_map<std::string, std::string> variableTypes; ///< Variable name -> record type name.
};

/// @brief Collects record type definitions and variable-type associations from source.
///
/// Parses the source into an AST and walks top-level statements:
/// - `RecordTypeDefStmt` -> extracts field names and type names
/// - `LetBindingStmt` -> if value is a `RecordExpr` with non-empty typeName, maps variable -> type
///
/// @param source The full document text.
/// @return Record field info and variable type associations.
[[nodiscard]] DocumentRecordInfo collectRecordInfo(std::string const& source);

/// @brief Computes all completion candidates for the given source and cursor position.
///
/// This is the shared entry point used by both shell and LSP.
/// It performs context analysis, generates candidates from the shared knowledge base
/// (keywords, builtins, constructors, dot-access, symbols), and combines with
/// caller-provided additional candidates.
///
/// @param source The input text (single line for shell, full document for LSP).
/// @param cursorByteOffset Cursor position as byte offset into source.
/// @param dataSource External data sources (symbols, record fields, extras).
/// @return Vector of completion candidates, unscored (caller adds scoring/filtering).
[[nodiscard]] std::vector<CompletionCandidate> computeCompletions(std::string_view source,
                                                                  size_t cursorByteOffset,
                                                                  CompletionDataSource const& dataSource);

} // namespace endo
