// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace endo
{

/// @brief Kind of a completion candidate.
enum class CompletionKind // NOLINT(performance-enum-size)
{
    Keyword,     ///< Language keyword (let, match, if, etc.)
    Function,    ///< User-defined function
    Variable,    ///< User-defined variable/value binding
    Constructor, ///< Type constructor (Some, None, Ok, Error)
    Module,      ///< Module name (Option)
    Field,       ///< Record field
    Builtin,     ///< Shell builtin command
    Property,    ///< Builtin property (read/write with <- syntax)
    Command,     ///< External command from PATH
    EnumValue,   ///< Enumerated parameter value (e.g., preset names)
    Other,       ///< Anything else (history, env vars, etc.)
};

/// @brief A completion candidate produced by the shared completion engine.
///
/// This is a protocol-neutral representation. The shell converts to tui::CompletionItem
/// (adding fuzzy scoring), the LSP converts to LSP CompletionItem JSON.
struct CompletionCandidate
{
    std::string text;        ///< The text to insert.
    std::string displayText; ///< Display text (may differ from insert text).
    std::string description; ///< Short description or synopsis.
    std::string detail;      ///< Longer detail text (e.g., function signature).
    CompletionKind kind = CompletionKind::Other;
};

/// @brief Lightweight symbol definition info usable by both shell and LSP for completion.
///
/// Both `FSharpPersistentState` (shell) and `SymbolCollector` (LSP) can map
/// their richer types into this struct for the shared completion engine.
struct SymbolDefinitionInfo
{
    std::string name;                                       ///< Symbol name.
    bool isFunction = false;                                ///< True if this is a function definition.
    std::vector<std::string> parameterNames;                ///< Parameter names (empty for non-functions).
    std::vector<std::optional<std::string>> parameterTypes; ///< Type annotations (parallel to params).
    std::optional<std::string> returnType;                  ///< Return type annotation.
    bool isRecursive = false;                               ///< Whether declared with `let rec`.
    bool isMutable = false;                                 ///< Whether declared with `let mut`.
};

/// @brief Field info for record type completion — carries name and type.
struct RecordFieldInfo
{
    std::string name;     ///< Field name (e.g., "age")
    std::string typeName; ///< Field type as string (e.g., "int", "str")
};

} // namespace endo
