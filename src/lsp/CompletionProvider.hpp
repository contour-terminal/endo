// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

#include "LspTypes.hpp"
#include <nlohmann/json.hpp>

namespace endo::lsp
{

/// @brief Computes completion items for the given document source and cursor position.
///
/// Uses the shared endo::computeCompletions() engine for context analysis and
/// candidate generation, then converts to LSP CompletionItem format.
///
/// @param source The full document text.
/// @param position The cursor position (0-based line and character).
/// @return JSON array of LSP CompletionItem objects.
[[nodiscard]] nlohmann::json computeCompletion(std::string const& source, Position position);

} // namespace endo::lsp
