// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

#include "LspTypes.hpp"

namespace endo::lsp
{

/// Computes formatting edits triggered by typing a character.
///
/// Supported trigger characters:
/// - '\n': auto-indent after block-opening constructs (=, ->, then, do, with)
/// - '|': align with previous match arm
///
/// @param source The full document text (after the character was typed)
/// @param position The cursor position after the character was typed
/// @param ch The character that was typed
/// @return A list of TextEdits to apply, or empty if no formatting needed
[[nodiscard]] std::vector<TextEdit> computeOnTypeFormatting(std::string const& source,
                                                            Position position,
                                                            std::string const& ch);

} // namespace endo::lsp
