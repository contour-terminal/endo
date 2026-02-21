// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tui/GenericSyntaxHighlighter.hpp>
#include <tui/TerminalOutput.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace endo::agent
{

/// @brief Type of a line in a unified diff.
enum class DiffLineType : std::uint8_t
{
    Context,  ///< Unchanged context line.
    Addition, ///< Added line (present in new text only).
    Deletion, ///< Deleted line (present in old text only).
    Hunk,     ///< Hunk header (@@ ... @@).
};

/// @brief A single line in a unified diff.
struct DiffLine
{
    DiffLineType type;  ///< Type of this diff line.
    std::string text;   ///< Line content (without leading +/-/space marker).
    int oldLineNum = 0; ///< Line number in old text (0 = not applicable).
    int newLineNum = 0; ///< Line number in new text (0 = not applicable).
};

/// @brief Maximum number of changed lines before the diff is truncated.
inline constexpr int LargeEditThreshold = 50;

/// @brief Generates a unified diff between two text strings.
///
/// Uses a line-based LCS (Longest Common Subsequence) algorithm to compute
/// the minimal set of changes, then formats them as unified diff hunks with
/// surrounding context lines.
///
/// @param oldText The original text.
/// @param newText The replacement text.
/// @param contextLines Number of unchanged context lines around changes (default: 3).
/// @return A vector of DiffLine structs representing the diff.
[[nodiscard]] auto generateUnifiedDiff(std::string_view oldText,
                                       std::string_view newText,
                                       int contextLines = 3) -> std::vector<DiffLine>;

/// @brief Renders a colored diff block to the terminal.
///
/// Outputs a unified diff with colored markers: green for additions,
/// red for deletions, dim for context lines. Each line is prefixed with
/// the agent left bar character.
///
/// @param output The terminal output to write to.
/// @param filePath The file path (displayed in the diff header).
/// @param diffLines The diff lines to render.
/// @param truncated If true, appends a truncation notice.
void renderDiff(tui::TerminalOutput& output,
                std::string_view filePath,
                std::span<DiffLine const> diffLines,
                bool truncated = false);

/// @brief Renders a syntax-highlighted diff block to the terminal.
///
/// Like renderDiff(), but applies syntax highlighting to the diff content:
/// - Addition lines: full syntax colors (bright).
/// - Context lines: syntax colors with dim = true.
/// - Deletion lines: monochrome red (preserves "removed" visual signal).
///
/// @param output The terminal output to write to.
/// @param filePath The file path (displayed in the diff header).
/// @param diffLines The diff lines to render.
/// @param language The language for syntax highlighting.
/// @param truncated If true, appends a truncation notice.
void renderDiff(tui::TerminalOutput& output,
                std::string_view filePath,
                std::span<DiffLine const> diffLines,
                tui::LanguageId language,
                bool truncated = false);

} // namespace endo::agent
