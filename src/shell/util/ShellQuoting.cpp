// SPDX-License-Identifier: Apache-2.0
#include "ShellQuoting.hpp"

#include <endo-language/lexer/Lexer.hpp>

#include <algorithm>

namespace endo
{

namespace
{
    /// Returns true if @p ch is a shell reserved word-terminator.
    /// Every reserved symbol is ASCII, so a byte comparison against the shared
    /// ShellReservedSymbols set is sufficient.
    /// @param ch The byte to test.
    /// @return True if the byte terminates a bare word.
    [[nodiscard]] bool isReservedByte(char ch)
    {
        auto const code = static_cast<char32_t>(static_cast<unsigned char>(ch));
        return ShellReservedSymbols.find(code) != std::u32string_view::npos;
    }
} // namespace

bool needsShellQuoting(std::string_view value)
{
    if (value.empty())
        return true;

    // A leading comment marker would swallow the token (or the rest of the line).
    if (value.starts_with("//") || value.starts_with("#"))
        return true;

    return std::ranges::any_of(value, isReservedByte);
}

std::string escapeDoubleQuoteContext(std::string_view value)
{
    std::string result;
    result.reserve(value.size());
    for (char const ch: value)
    {
        if (ch == '\\' || ch == '"' || ch == '$' || ch == '`')
            result += '\\';
        result += ch;
    }
    return result;
}

std::string shellQuoteDouble(std::string_view value)
{
    std::string result;
    result.reserve(value.size() + 2);
    result += '"';
    result += escapeDoubleQuoteContext(value);
    result += '"';
    return result;
}

std::string quoteCompletionValue(std::string_view text, char precedingChar)
{
    // A directory candidate has more to complete, so keep the quote open.
    bool const isDirectory = text.ends_with('/');
    auto closeIfFinal = [&](std::string value, char quote) {
        if (!isDirectory)
            value += quote;
        return value;
    };

    // Already inside an open double quote (e.g. `& "X:/Work<TAB>`): escape for the
    // double-quote context instead of opening a new quote.
    if (precedingChar == '"')
        return closeIfFinal(escapeDoubleQuoteContext(text), '"');

    // Already inside an open single quote: single-quoted content is fully literal.
    // (A value containing a single quote cannot be represented here; left to the user.)
    if (precedingChar == '\'')
        return closeIfFinal(std::string(text), '\'');

    // Bare word: wrap in double quotes only when it would otherwise not read as a
    // single parameter (spaces, '!', '$', a leading comment marker, etc.).
    if (needsShellQuoting(text))
        return closeIfFinal('"' + escapeDoubleQuoteContext(text), '"');

    return std::string(text);
}

} // namespace endo
