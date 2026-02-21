// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tui/TerminalOutput.hpp>

#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace tui
{

struct Theme;

/// @brief Category of a syntax token for colorization.
enum class HighlightCategory : std::uint8_t
{
    Default,      ///< Unclassified text.
    Keyword,      ///< Language keywords (if, else, for, etc.).
    Number,       ///< Numeric literals.
    String,       ///< String literals and delimiters.
    Operator,     ///< Operators (+, -, |>, etc.).
    Variable,     ///< Variables ($VAR, ${VAR}).
    Constructor,  ///< Constructors and type constructors.
    Comment,      ///< Comments (line and block).
    Type,         ///< Type names and annotations.
    Punctuation,  ///< Brackets, semicolons, commas.
    Function,     ///< Built-in functions and commands.
    Preprocessor, ///< Preprocessor directives (#include, #define).
};

/// @brief Supported languages for syntax highlighting.
enum class LanguageId : std::uint8_t
{
    None,     ///< No language — no highlighting applied.
    Cpp,      ///< C and C++.
    CMake,    ///< CMake build scripts.
    Python,   ///< Python.
    Bash,     ///< Bash/sh shell scripts.
    Markdown, ///< Markdown.
    Json,     ///< JSON.
    Yaml,     ///< YAML.
    GitDiff,  ///< Git diff output.
    Assembly, ///< x86 assembly (Intel and AT&T syntax).
};

/// @brief State carried across lines for multi-line constructs.
enum class HighlightState : std::uint8_t
{
    Normal,            ///< Normal scanning state.
    BlockComment,      ///< Inside a /* ... */ block comment.
    RawString,         ///< Inside a C++ R"(...)" raw string.
    TripleQuoteString, ///< Inside a Python """ or ''' triple-quoted string.
};

/// @brief Per-character highlight category for a line.
using HighlightMap = std::vector<HighlightCategory>;

/// @brief Detects language from a file extension (e.g. ".cpp", ".py").
/// @param ext The file extension including the leading dot.
/// @return The detected language, or LanguageId::None.
[[nodiscard]] constexpr auto detectLanguageFromExtension(std::string_view ext) -> LanguageId;

/// @brief Detects language from a markdown fence tag (e.g. "cpp", "python").
/// @param tag The fence language tag (without the backticks).
/// @return The detected language, or LanguageId::None.
[[nodiscard]] constexpr auto detectLanguageFromFenceTag(std::string_view tag) -> LanguageId;

/// @brief Detects language from a full file path by extracting the extension.
/// @param filePath The file path.
/// @return The detected language, or LanguageId::None.
[[nodiscard]] auto detectLanguageFromPath(std::string_view filePath) -> LanguageId;

/// @brief Highlights a single line of source code.
///
/// Performs a single-pass scan of the line, classifying each character position
/// into a HighlightCategory. Supports multi-line state (block comments, raw strings,
/// triple-quoted strings) via the state parameter.
///
/// @param line The source line to highlight.
/// @param language The language to use for highlighting rules.
/// @param state The current multi-line state (from previous line's output).
/// @return A pair of (per-character highlight map, updated state for next line).
[[nodiscard]] auto highlightLine(std::string_view line,
                                 LanguageId language,
                                 HighlightState state = HighlightState::Normal)
    -> std::pair<HighlightMap, HighlightState>;

/// @brief Maps a highlight category to its color from the theme's syntax palette.
/// @param cat The highlight category.
/// @param theme The current theme.
/// @return The RGB color for the category.
[[nodiscard]] auto categoryColor(HighlightCategory cat, Theme const& theme) -> RgbColor;

/// @brief Renders a syntax-highlighted line to the terminal.
///
/// Writes the text as a sequence of colored spans, grouping consecutive characters
/// with the same highlight category into single writeText calls for efficiency.
///
/// @param output The terminal output to write to.
/// @param text The source text.
/// @param highlights Per-character highlight categories (must be same length as text).
/// @param baseStyle Base style to apply (highlight colors override the fg).
/// @param theme The current theme for color lookup.
void renderHighlightedLine(TerminalOutput& output,
                           std::string_view text,
                           HighlightMap const& highlights,
                           Style baseStyle,
                           Theme const& theme);

// --- Inline constexpr implementations ---

constexpr auto detectLanguageFromExtension(std::string_view ext) -> LanguageId
{
    if (ext == ".cpp" || ext == ".cxx" || ext == ".cc" || ext == ".c" || ext == ".hpp" || ext == ".hxx"
        || ext == ".hh" || ext == ".h" || ext == ".ipp")
        return LanguageId::Cpp;
    if (ext == ".py" || ext == ".pyw" || ext == ".pyi")
        return LanguageId::Python;
    if (ext == ".sh" || ext == ".bash" || ext == ".zsh")
        return LanguageId::Bash;
    if (ext == ".json" || ext == ".jsonl")
        return LanguageId::Json;
    if (ext == ".yml" || ext == ".yaml")
        return LanguageId::Yaml;
    if (ext == ".md" || ext == ".markdown")
        return LanguageId::Markdown;
    if (ext == ".cmake")
        return LanguageId::CMake;
    if (ext == ".diff" || ext == ".patch")
        return LanguageId::GitDiff;
    if (ext == ".asm" || ext == ".s" || ext == ".S" || ext == ".nasm")
        return LanguageId::Assembly;
    return LanguageId::None;
}

constexpr auto detectLanguageFromFenceTag(std::string_view tag) -> LanguageId
{
    if (tag == "cpp" || tag == "c++" || tag == "cxx" || tag == "c" || tag == "h" || tag == "hpp")
        return LanguageId::Cpp;
    if (tag == "python" || tag == "py")
        return LanguageId::Python;
    if (tag == "bash" || tag == "sh" || tag == "shell" || tag == "zsh")
        return LanguageId::Bash;
    if (tag == "json" || tag == "jsonl")
        return LanguageId::Json;
    if (tag == "yaml" || tag == "yml")
        return LanguageId::Yaml;
    if (tag == "markdown" || tag == "md")
        return LanguageId::Markdown;
    if (tag == "cmake")
        return LanguageId::CMake;
    if (tag == "diff" || tag == "patch")
        return LanguageId::GitDiff;
    if (tag == "asm" || tag == "assembly" || tag == "nasm" || tag == "x86" || tag == "x86asm"
        || tag == "intel" || tag == "att" || tag == "gas")
        return LanguageId::Assembly;
    return LanguageId::None;
}

} // namespace tui
