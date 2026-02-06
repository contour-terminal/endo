// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/CompletionProvider.hpp>

#include <string_view>

namespace endo
{

/// @brief Analyzes input to determine completion context.
///
/// Uses the shell lexer to tokenize input and determine what type
/// of completion is appropriate at the cursor position.
class CompletionContextAnalyzer
{
  public:
    /// @brief Analyzes input to determine completion context at cursor.
    /// @param input The full input line.
    /// @param cursorPosition The cursor byte offset in input.
    /// @return The completion context.
    [[nodiscard]] static CompletionContext analyze(std::string_view input, size_t cursorPosition);

  private:
    /// @brief Finds word boundaries around the cursor.
    /// @param input The input string.
    /// @param cursorPosition The cursor position.
    /// @return Pair of (wordStart, wordEnd) byte offsets.
    [[nodiscard]] static std::pair<size_t, size_t> findWordBoundaries(std::string_view input,
                                                                      size_t cursorPosition);

    /// @brief Checks if a character is a word-breaking character.
    [[nodiscard]] static bool isWordBreak(char ch);

    /// @brief Checks if a character starts a variable reference.
    [[nodiscard]] static bool isVariableStart(std::string_view input, size_t pos);

    /// @brief Checks if the prefix looks like a file path.
    [[nodiscard]] static bool looksLikeFilePath(std::string_view prefix);

    /// @brief Checks if the prefix looks like an option.
    [[nodiscard]] static bool looksLikeOption(std::string_view prefix);
};

} // namespace endo
