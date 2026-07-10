// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <string_view>

namespace endo
{

/// @brief Tests whether a value must be quoted to survive as a single shell token.
///
/// A bare (unquoted) word cannot contain characters that the lexer treats as word
/// terminators (see @ref ShellReservedSymbols: whitespace and `| < > ( ) ! $ ' "
/// ; ` and backtick) nor begin a comment (`//` C-style or `#` shell-style), or it
/// would split into several tokens (or be swallowed as a comment). This predicate
/// reports when quoting is required so the value reads as one parameter.
///
/// @param value The candidate word (UTF-8).
/// @return True if @p value needs quoting to remain a single token; false for a
///         value that is already a valid bare word.
[[nodiscard]] bool needsShellQuoting(std::string_view value);

/// @brief Escapes a value for insertion inside an already-open double-quoted string.
///
/// Prefixes a backslash before each character the double-quoted-string lexer treats
/// specially — backslash, double quote, `$`, and backtick — so the escaped text is
/// consumed verbatim. No surrounding quotes are added.
///
/// @param value The string to escape (UTF-8).
/// @return The escaped representation, without surrounding quotes.
[[nodiscard]] std::string escapeDoubleQuoteContext(std::string_view value);

/// @brief Wraps a value in double quotes, escaping inner special characters.
///
/// Produces `"<escaped value>"` such that the result parses back to exactly
/// @p value as a single token. Inner backslash, double quote, `$`, and backtick are
/// escaped via @ref escapeDoubleQuoteContext.
///
/// @param value The string to quote (UTF-8).
/// @return The double-quoted, escaped representation.
[[nodiscard]] std::string shellQuoteDouble(std::string_view value);

/// @brief Renders a completion value for insertion so it stays a single token.
///
/// Given the character immediately preceding the text being replaced, produces the
/// string to splice into the input buffer:
/// - inside an open double quote (@p precedingChar == '"'): escape for that context
///   and close the quote for a final candidate;
/// - inside an open single quote (@p precedingChar == '\''): insert literally and
///   close for a final candidate (single-quoted content is not escapable);
/// - otherwise: wrap in double quotes when @ref needsShellQuoting reports the value
///   would not survive bare, else insert unchanged.
///
/// A @em directory candidate (trailing '/') has more to complete, so its quote is
/// left open for the next completion; a final candidate closes it.
///
/// @param text The raw completion value.
/// @param precedingChar The byte just before the replaced prefix ('"'/'\'' when
///        completing inside an open quote; '\0' at the start of input).
/// @return The text to insert into the buffer.
[[nodiscard]] std::string quoteCompletionValue(std::string_view text, char precedingChar);

} // namespace endo
