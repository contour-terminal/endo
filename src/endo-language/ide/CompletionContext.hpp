// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace endo
{

/// @brief Type of completion context.
enum class CompletionContextType // NOLINT(performance-enum-size)
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

/// @brief Why a completion was requested.
///
/// Distinguishes a speculative ghost-text/autosuggestion query (fired ~100ms after
/// every keystroke) from an explicit user Tab press. Providers that perform expensive
/// or blocking work must not do it for @c Autosuggest — they serve cached data only —
/// so typing never stalls the prompt. (See @c ScriptedCompleter, whose dnf/rpm
/// completers shell out to package managers and so are cache-only for autosuggest.)
enum class CompletionIntent : std::uint8_t
{
    Autosuggest, ///< Ghost-text suggestion; providers must be cheap / cache-only.
    Explicit     ///< User pressed Tab; providers may perform expensive work.
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

    /// Why this completion was requested. Defaults to @c Explicit so every existing
    /// caller (and the whole test suite) keeps the "may do expensive work" behaviour;
    /// only the ghost-text path (@c Completer::suggest) sets @c Autosuggest.
    CompletionIntent intent = CompletionIntent::Explicit;
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

    /// @brief Checks if the prefix looks like a file path.
    [[nodiscard]] static bool looksLikeFilePath(std::string_view prefix);

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

    /// @brief Checks if the prefix looks like an option.
    [[nodiscard]] static bool looksLikeOption(std::string_view prefix);
};

} // namespace endo
