// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

#include <endo-language/HoverInfo.hpp>

namespace endo
{

/// @brief Diagnostic severity levels (matching LSP specification).
enum class DiagnosticSeverity : int
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
};

/// Collects diagnostics by parsing the given source.
///
/// Creates a temporary parser with a stub runtime and collects any parse errors,
/// converting them into DiagnosticMessage objects with 0-based positions.
///
/// @param source The full document text to parse
/// @return A vector of diagnostics (empty if the source parses cleanly)
[[nodiscard]] std::vector<DiagnosticMessage> collectDiagnostics(std::string const& source);

} // namespace endo
