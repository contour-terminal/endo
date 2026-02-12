// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace endo
{

/// @brief Type of completion context.
enum class CompletionContextType
{
    Command,       ///< First token position - complete executables/builtins.
    Argument,      ///< General argument position.
    FilePath,      ///< Path argument (starts with /, ./, ~).
    Variable,      ///< After $ (variable expansion).
    VariableBrace, ///< Inside ${...} (brace variable expansion).
    Redirect,      ///< After < or > (file target).
    Option,        ///< After - or -- (command option).
    Unknown        ///< Unable to determine context.
};

/// @brief Context information for completion.
struct CompletionContext
{
    CompletionContextType type = CompletionContextType::Unknown;
    std::string prefix;                 ///< Word being completed (may be empty).
    size_t prefixStart = 0;             ///< Byte offset of prefix in input.
    size_t cursorPosition = 0;          ///< Cursor byte offset in input.
    std::optional<std::string> command; ///< Current command (for option context).
    std::string fullInput;              ///< Complete input line.
};

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
