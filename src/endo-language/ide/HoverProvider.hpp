// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <endo-language/ide/HoverInfo.hpp>

#include <optional>
#include <string>

namespace endo
{

/// Computes hover information for a given cursor position in the source.
///
/// Tokenizes the source and finds the token at the cursor position, then returns
/// a markdown description based on the token type (keywords, constructors, operators, builtins,
/// and user-defined bindings).
///
/// @param source The full document text
/// @param position The cursor position (0-based line and character)
/// @return Hover information if a meaningful token was found, otherwise std::nullopt
[[nodiscard]] std::optional<HoverInfo> computeHover(std::string const& source, SourcePosition position);

} // namespace endo
