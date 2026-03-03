// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <endo-language/lexer/Lexer.hpp>

#include <optional>
#include <string>
#include <vector>

#include "LspTypes.hpp"

namespace endo::lsp
{

/// Categorizes the kind of symbol definition.
enum class SymbolCategory
{
    Variable,     ///< Simple variable binding
    Function,     ///< Function definition (has parameters)
    Parameter,    ///< Function parameter or pattern binding
    RecordType,   ///< Record type definition (`type T = { ... }`)
    RecordField,  ///< Field within a record type
    UnionType,    ///< Discriminated union type definition (`type T = | ...`)
    UnionVariant, ///< Variant constructor within a union type
    Property,     ///< Property binding (get/set)
};

/// Represents a symbol definition (variable, function, parameter, type, field, variant).
struct SymbolDefinition
{
    std::string name;                                   ///< Symbol name
    SourceLocationRange location;                       ///< Location of the name in source
    SymbolCategory category = SymbolCategory::Variable; ///< Kind of symbol
    std::vector<std::string> parameterNames;            ///< Parameter names (empty for non-functions)
    std::vector<std::string> parameterTypes;            ///< Parameter type annotations (empty string if none)
    std::optional<std::string> returnType;              ///< Return type annotation
    std::optional<std::string> detail;                  ///< Type signature or other detail string
    std::optional<std::string> enclosingSymbol;         ///< Name of the enclosing symbol (for children)
    int scopeId = 0;                                    ///< Unique scope identifier
    int nestingDepth = 0;                               ///< Scope nesting depth (0 = top-level)
};

/// Represents a reference to a symbol.
struct SymbolReference
{
    std::string name;             ///< Symbol name
    SourceLocationRange location; ///< Location of the reference in source
    int definitionIndex = -1;     ///< Index into SymbolTable::definitions (-1 if unresolved)
};

/// Collected symbol information from a source file.
struct SymbolTable
{
    std::vector<SymbolDefinition> definitions;
    std::vector<SymbolReference> references;
};

/// Collects all symbols (definitions and references) from the given source.
/// @param source The full document text
/// @return Symbol table if parsing succeeds, otherwise std::nullopt
[[nodiscard]] std::optional<SymbolTable> collectSymbols(std::string const& source);

/// Finds the definition location of the symbol at the given cursor position.
/// @param source The full document text
/// @param position The 0-based cursor position
/// @return Definition location if found, otherwise std::nullopt
[[nodiscard]] std::optional<SourceLocationRange> findDefinition(std::string const& source, Position position);

/// Finds all references to the symbol at the given cursor position.
/// @param source The full document text
/// @param position The 0-based cursor position
/// @param includeDeclaration Whether to include the declaration itself
/// @return Vector of reference locations (may be empty)
[[nodiscard]] std::vector<SourceLocationRange> findReferences(std::string const& source,
                                                              Position position,
                                                              bool includeDeclaration);

/// Finds the source range of the identifier at the given cursor position.
/// @param source The full document text
/// @param position The 0-based cursor position
/// @return Source range of the identifier, or std::nullopt if not on an identifier
[[nodiscard]] std::optional<SourceLocationRange> findSymbolRangeAt(std::string const& source,
                                                                   Position position);

/// A highlight entry: source range and whether it is a definition (write) or reference (read).
struct HighlightEntry
{
    SourceLocationRange range;
    bool isDefinition = false; ///< true for definitions (Write kind), false for references (Read kind)
};

/// Finds all highlights for the symbol at the given cursor position.
///
/// Returns the definition with isDefinition=true and all references with isDefinition=false.
/// @param source The full document text
/// @param position The 0-based cursor position
/// @return Vector of highlight entries (may be empty)
[[nodiscard]] std::vector<HighlightEntry> findHighlights(std::string const& source, Position position);

} // namespace endo::lsp
