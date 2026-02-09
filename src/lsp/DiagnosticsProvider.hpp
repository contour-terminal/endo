// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <CoreVM/CoreVM.hpp>

#include <string>
#include <string_view>
#include <vector>

#include "LspTypes.hpp"

namespace endo::lsp
{

/// Computes LSP diagnostics by parsing the given source.
///
/// Creates a temporary parser with a stub runtime and collects any parse errors,
/// converting them into LSP Diagnostic objects.
///
/// @param source The full document text to parse
/// @param fileName A label for the source (e.g., the URI)
/// @param runtime A CoreVM runtime with registered builtins (callproc, print, println)
/// @return A vector of diagnostics (empty if the source parses cleanly)
[[nodiscard]] std::vector<Diagnostic> computeDiagnostics(std::string const& source,
                                                         std::string_view fileName,
                                                         CoreVM::Runtime& runtime);

} // namespace endo::lsp
