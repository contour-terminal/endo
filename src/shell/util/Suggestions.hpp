// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <endo-language/lexer/Lexer.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace endo
{

/// Generates suggestions for typos and syntax errors.
class SuggestionGenerator
{
  public:
    /// Computes the Levenshtein edit distance between two strings.
    ///
    /// @param a First string
    /// @param b Second string
    /// @return The minimum number of single-character edits (insertions, deletions, substitutions)
    [[nodiscard]] static size_t levenshteinDistance(std::string_view a, std::string_view b);

    /// Suggests commands similar to the input based on edit distance.
    ///
    /// @param input The mistyped command
    /// @param builtins List of valid builtin command names
    /// @param maxDistance Maximum edit distance to consider (default: 3)
    /// @return List of suggestions sorted by edit distance
    [[nodiscard]] static std::vector<std::string> suggestCommand(std::string_view input,
                                                                 std::span<std::string_view const> builtins,
                                                                 size_t maxDistance = 3);

    /// Suggests syntax fixes based on the expected vs actual token.
    ///
    /// @param expected The token that was expected
    /// @param got The token that was found
    /// @return List of suggestion strings describing how to fix the error
    [[nodiscard]] static std::vector<std::string> suggestSyntaxFix(Token expected, Token got);

    /// Generates a "Did you mean 'X'?" suggestion string.
    ///
    /// @param suggestion The suggested correction
    /// @return Formatted suggestion string
    [[nodiscard]] static std::string formatDidYouMean(std::string_view suggestion);
};

} // namespace endo
