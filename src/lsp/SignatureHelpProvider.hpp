// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>

#include "LspTypes.hpp"

namespace endo::lsp
{

/// Computes signature help for the function call at the given cursor position.
///
/// Finds the innermost function application containing the cursor, determines which
/// function is being called, and provides parameter information.
///
/// @param source The full document text
/// @param position The cursor position (0-based line and character)
/// @return Signature help if the cursor is in a function call context, otherwise std::nullopt
[[nodiscard]] std::optional<SignatureHelp> computeSignatureHelp(std::string const& source, Position position);

} // namespace endo::lsp
