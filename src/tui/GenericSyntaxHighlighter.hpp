// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tui/TerminalOutput.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
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
    None,       ///< No language — no highlighting applied.
    Cpp,        ///< C and C++.
    CMake,      ///< CMake build scripts.
    Python,     ///< Python.
    Bash,       ///< Bash/sh shell scripts.
    Markdown,   ///< Markdown.
    Json,       ///< JSON.
    Yaml,       ///< YAML.
    GitDiff,    ///< Git diff output.
    Assembly,   ///< x86 assembly (Intel and AT&T syntax).
    PowerShell, ///< PowerShell scripts.
    Cmd,        ///< Windows CMD / batch scripts.
    Xml,        ///< XML and XML-based dialects (.props, .csproj, .xaml, .svg, …).
    Ini,        ///< INI / .editorconfig configuration files.
    Endo,       ///< Endo shell language (via registered callback).
};

/// @brief State carried across lines for multi-line constructs.
enum class HighlightState : std::uint8_t
{
    Normal,            ///< Normal scanning state.
    BlockComment,      ///< Inside a /* ... */ block comment.
    RawString,         ///< Inside a C++ R"(...)" raw string.
    TripleQuoteString, ///< Inside a Python """ or ''' triple-quoted string.
    XmlComment,        ///< Inside an XML <!-- ... --> comment.
    PsBlockComment,    ///< Inside a PowerShell <# ... #> block comment.
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

/// @brief Renders a syntax-highlighted line to a string with ANSI SGR color codes.
///
/// Produces a string with embedded true-color ANSI escape sequences
/// (\033[38;2;r;g;bm) for each colored span, terminated with a reset (\033[m).
///
/// @param text The source text.
/// @param highlights Per-character highlight categories (must be same length as text).
/// @param theme The current theme for color lookup.
/// @return A string containing the text with embedded ANSI color codes.
[[nodiscard]] auto renderHighlightedLineToString(std::string_view text,
                                                 HighlightMap const& highlights,
                                                 Theme const& theme) -> std::string;

/// @brief Callback type for external language highlighters.
using HighlightFunction =
    std::function<std::pair<HighlightMap, HighlightState>(std::string_view line, HighlightState state)>;

/// @brief Registers the Endo language highlighter callback.
///
/// Called once at shell startup to wire the Endo tokenizer into the
/// generic highlighter without creating a build dependency from tui
/// to endo-language.
///
/// @param fn The highlighting function to call for LanguageId::Endo.
void registerEndoHighlighter(HighlightFunction fn);

// --- Data-driven language detection tables ---

/// @brief Associates a textual token (file extension or fence tag) with a language.
struct LanguageToken
{
    std::string_view token; ///< The extension (with leading dot) or fence tag to match.
    LanguageId language;    ///< The language this token maps to.
};

/// @brief File extension → language table (extensions include the leading dot).
///
/// Each row maps one extension to one language; add a row to teach the highlighter a
/// new extension. Several distinct languages are intentionally folded onto a shared
/// highlighter (e.g. JS/TS/Rust/Go reuse the C-family highlighter, TOML reuses YAML).
inline constexpr auto ExtensionLanguageTable = std::to_array<LanguageToken>({
    // C / C++ and C-family languages sharing the C++ highlighter.
    { .token = ".cpp", .language = LanguageId::Cpp },
    { .token = ".cxx", .language = LanguageId::Cpp },
    { .token = ".cc", .language = LanguageId::Cpp },
    { .token = ".c", .language = LanguageId::Cpp },
    { .token = ".hpp", .language = LanguageId::Cpp },
    { .token = ".hxx", .language = LanguageId::Cpp },
    { .token = ".hh", .language = LanguageId::Cpp },
    { .token = ".h", .language = LanguageId::Cpp },
    { .token = ".ipp", .language = LanguageId::Cpp },
    { .token = ".js", .language = LanguageId::Cpp },
    { .token = ".jsx", .language = LanguageId::Cpp },
    { .token = ".ts", .language = LanguageId::Cpp },
    { .token = ".tsx", .language = LanguageId::Cpp },
    { .token = ".rs", .language = LanguageId::Cpp },
    { .token = ".go", .language = LanguageId::Cpp },
    { .token = ".java", .language = LanguageId::Cpp },
    { .token = ".kt", .language = LanguageId::Cpp },
    { .token = ".cs", .language = LanguageId::Cpp },
    // Python and Python-like languages.
    { .token = ".py", .language = LanguageId::Python },
    { .token = ".pyw", .language = LanguageId::Python },
    { .token = ".pyi", .language = LanguageId::Python },
    { .token = ".rb", .language = LanguageId::Python },
    { .token = ".lua", .language = LanguageId::Python },
    // Shell.
    { .token = ".sh", .language = LanguageId::Bash },
    { .token = ".bash", .language = LanguageId::Bash },
    { .token = ".zsh", .language = LanguageId::Bash },
    // Data / config.
    { .token = ".json", .language = LanguageId::Json },
    { .token = ".jsonl", .language = LanguageId::Json },
    { .token = ".yml", .language = LanguageId::Yaml },
    { .token = ".yaml", .language = LanguageId::Yaml },
    { .token = ".toml", .language = LanguageId::Yaml },
    { .token = ".ini", .language = LanguageId::Ini },
    // Markup / build / diff.
    { .token = ".md", .language = LanguageId::Markdown },
    { .token = ".markdown", .language = LanguageId::Markdown },
    { .token = ".cmake", .language = LanguageId::CMake },
    { .token = ".diff", .language = LanguageId::GitDiff },
    { .token = ".patch", .language = LanguageId::GitDiff },
    { .token = ".asm", .language = LanguageId::Assembly },
    { .token = ".s", .language = LanguageId::Assembly },
    { .token = ".S", .language = LanguageId::Assembly },
    { .token = ".nasm", .language = LanguageId::Assembly },
    // PowerShell.
    { .token = ".ps1", .language = LanguageId::PowerShell },
    { .token = ".psm1", .language = LanguageId::PowerShell },
    { .token = ".psd1", .language = LanguageId::PowerShell },
    { .token = ".ps1xml", .language = LanguageId::PowerShell },
    // Windows CMD / batch.
    { .token = ".cmd", .language = LanguageId::Cmd },
    { .token = ".bat", .language = LanguageId::Cmd },
    // XML and XML-based dialects.
    { .token = ".xml", .language = LanguageId::Xml },
    { .token = ".props", .language = LanguageId::Xml },
    { .token = ".csproj", .language = LanguageId::Xml },
    { .token = ".targets", .language = LanguageId::Xml },
    { .token = ".vcxproj", .language = LanguageId::Xml },
    { .token = ".nuspec", .language = LanguageId::Xml },
    { .token = ".resx", .language = LanguageId::Xml },
    { .token = ".xaml", .language = LanguageId::Xml },
    { .token = ".svg", .language = LanguageId::Xml },
    { .token = ".plist", .language = LanguageId::Xml },
    { .token = ".xsd", .language = LanguageId::Xml },
    { .token = ".wxs", .language = LanguageId::Xml },
    // Endo.
    { .token = ".endo", .language = LanguageId::Endo },
});

/// @brief Markdown fence tag → language table (lowercase tags, without backticks).
inline constexpr auto FenceTagLanguageTable = std::to_array<LanguageToken>({
    { .token = "cpp", .language = LanguageId::Cpp },
    { .token = "c++", .language = LanguageId::Cpp },
    { .token = "cxx", .language = LanguageId::Cpp },
    { .token = "c", .language = LanguageId::Cpp },
    { .token = "h", .language = LanguageId::Cpp },
    { .token = "hpp", .language = LanguageId::Cpp },
    { .token = "javascript", .language = LanguageId::Cpp },
    { .token = "js", .language = LanguageId::Cpp },
    { .token = "jsx", .language = LanguageId::Cpp },
    { .token = "typescript", .language = LanguageId::Cpp },
    { .token = "ts", .language = LanguageId::Cpp },
    { .token = "tsx", .language = LanguageId::Cpp },
    { .token = "rust", .language = LanguageId::Cpp },
    { .token = "rs", .language = LanguageId::Cpp },
    { .token = "go", .language = LanguageId::Cpp },
    { .token = "golang", .language = LanguageId::Cpp },
    { .token = "java", .language = LanguageId::Cpp },
    { .token = "kotlin", .language = LanguageId::Cpp },
    { .token = "kt", .language = LanguageId::Cpp },
    { .token = "csharp", .language = LanguageId::Cpp },
    { .token = "cs", .language = LanguageId::Cpp },
    { .token = "c#", .language = LanguageId::Cpp },
    { .token = "python", .language = LanguageId::Python },
    { .token = "py", .language = LanguageId::Python },
    { .token = "ruby", .language = LanguageId::Python },
    { .token = "rb", .language = LanguageId::Python },
    { .token = "lua", .language = LanguageId::Python },
    { .token = "bash", .language = LanguageId::Bash },
    { .token = "sh", .language = LanguageId::Bash },
    { .token = "shell", .language = LanguageId::Bash },
    { .token = "zsh", .language = LanguageId::Bash },
    { .token = "dockerfile", .language = LanguageId::Bash },
    { .token = "docker", .language = LanguageId::Bash },
    { .token = "json", .language = LanguageId::Json },
    { .token = "jsonl", .language = LanguageId::Json },
    { .token = "yaml", .language = LanguageId::Yaml },
    { .token = "yml", .language = LanguageId::Yaml },
    { .token = "toml", .language = LanguageId::Yaml },
    { .token = "markdown", .language = LanguageId::Markdown },
    { .token = "md", .language = LanguageId::Markdown },
    { .token = "cmake", .language = LanguageId::CMake },
    { .token = "diff", .language = LanguageId::GitDiff },
    { .token = "patch", .language = LanguageId::GitDiff },
    { .token = "asm", .language = LanguageId::Assembly },
    { .token = "assembly", .language = LanguageId::Assembly },
    { .token = "nasm", .language = LanguageId::Assembly },
    { .token = "x86", .language = LanguageId::Assembly },
    { .token = "x86asm", .language = LanguageId::Assembly },
    { .token = "intel", .language = LanguageId::Assembly },
    { .token = "att", .language = LanguageId::Assembly },
    { .token = "gas", .language = LanguageId::Assembly },
    { .token = "powershell", .language = LanguageId::PowerShell },
    { .token = "pwsh", .language = LanguageId::PowerShell },
    { .token = "ps", .language = LanguageId::PowerShell },
    { .token = "ps1", .language = LanguageId::PowerShell },
    { .token = "posh", .language = LanguageId::PowerShell },
    { .token = "bat", .language = LanguageId::Cmd },
    { .token = "batch", .language = LanguageId::Cmd },
    { .token = "cmd", .language = LanguageId::Cmd },
    { .token = "dosbatch", .language = LanguageId::Cmd },
    { .token = "dos", .language = LanguageId::Cmd },
    { .token = "xml", .language = LanguageId::Xml },
    { .token = "xaml", .language = LanguageId::Xml },
    { .token = "svg", .language = LanguageId::Xml },
    { .token = "html", .language = LanguageId::Xml },
    { .token = "xhtml", .language = LanguageId::Xml },
    { .token = "ini", .language = LanguageId::Ini },
    { .token = "editorconfig", .language = LanguageId::Ini },
    { .token = "dosini", .language = LanguageId::Ini },
    { .token = "endo", .language = LanguageId::Endo },
});

/// @brief Looks up a token in a language table, returning the mapped language or None.
/// @param table The token→language table to search.
/// @param token The token to look up (exact match).
/// @return The mapped language, or LanguageId::None if the token is not present.
template <std::size_t N>
[[nodiscard]] constexpr auto lookupLanguage(std::array<LanguageToken, N> const& table, std::string_view token)
    -> LanguageId
{
    auto const it = std::ranges::find(table, token, &LanguageToken::token);
    return it != table.end() ? it->language : LanguageId::None;
}

// --- Inline constexpr implementations ---

constexpr auto detectLanguageFromExtension(std::string_view ext) -> LanguageId
{
    return lookupLanguage(ExtensionLanguageTable, ext);
}

constexpr auto detectLanguageFromFenceTag(std::string_view tag) -> LanguageId
{
    return lookupLanguage(FenceTagLanguageTable, tag);
}

} // namespace tui
