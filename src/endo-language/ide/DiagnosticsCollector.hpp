// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <endo-language/ide/HoverInfo.hpp>

#include <set>
#include <string>
#include <vector>

namespace endo
{

/// @brief Diagnostic severity levels (matching LSP specification).
enum class DiagnosticSeverity : int // NOLINT(performance-enum-size)
{
    Error = 1,
    Warning = 2,
    Information = 3,
    Hint = 4,
};

/// @brief A diagnostic message with source range and severity.
struct DiagnosticMessage
{
    SourceRange range;
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string message;
    std::vector<std::string> suggestions; ///< Optional fix suggestions (displayed as hints)
};

/// Collects diagnostics by parsing the given source.
///
/// Creates a temporary parser with a stub runtime and collects any parse errors,
/// converting them into DiagnosticMessage objects with 0-based positions.
///
/// @param source The full document text to parse
/// @param knownNames Optional set of externally known names (e.g. persisted F# functions from prior
///                   REPL prompts) that should not trigger "command not found" diagnostics.
/// @return A vector of diagnostics (empty if the source parses cleanly)
[[nodiscard]] std::vector<DiagnosticMessage> collectDiagnostics(std::string const& source,
                                                                std::set<std::string> const& knownNames = {});

} // namespace endo
