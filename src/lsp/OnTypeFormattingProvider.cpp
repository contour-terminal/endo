// SPDX-License-Identifier: Apache-2.0
#include "OnTypeFormattingProvider.hpp"

#include <algorithm>
#include <string>

#include "LspUtils.hpp"

namespace endo::lsp
{

namespace
{
    /// Returns the leading whitespace of a line.
    [[nodiscard]] std::string getIndent(std::string const& line)
    {
        auto const pos = line.find_first_not_of(" \t");
        if (pos == std::string::npos)
            return line;
        return line.substr(0, pos);
    }

    /// Returns the trimmed content of a line.
    [[nodiscard]] std::string trimRight(std::string const& line)
    {
        auto const pos = line.find_last_not_of(" \t\r\n");
        if (pos == std::string::npos)
            return {};
        return line.substr(0, pos + 1);
    }

    /// Checks if a line ends with a block-opening construct.
    [[nodiscard]] bool endsWithBlockOpener(std::string const& trimmedLine)
    {
        static constexpr std::string_view Openers[] = { "=", "->", "then", "do", "with" };
        for (auto const opener: Openers)
        {
            if (trimmedLine.ends_with(opener))
            {
                // Ensure it's a word boundary for keyword openers
                if (opener.size() > 2 && trimmedLine.size() > opener.size())
                {
                    auto const preceding = trimmedLine[trimmedLine.size() - opener.size() - 1];
                    if (std::isalnum(preceding) || preceding == '_')
                        continue;
                }
                return true;
            }
        }
        return false;
    }

} // namespace

std::vector<TextEdit> computeOnTypeFormatting(std::string const& source,
                                              Position position,
                                              std::string const& ch)
{
    auto const lines = splitLines(source);

    if (ch == "\n")
    {
        // Look at the previous line for block-opening constructs
        auto const prevLineIdx = position.line - 1;
        if (prevLineIdx < 0 || std::cmp_greater_equal(prevLineIdx, lines.size()))
            return {};

        auto const& prevLine = lines[static_cast<size_t>(prevLineIdx)];
        auto const trimmed = trimRight(prevLine);
        if (trimmed.empty())
            return {};

        if (endsWithBlockOpener(trimmed))
        {
            auto const currentIndent = getIndent(prevLine);
            auto const newIndent = currentIndent + "    ";

            // Check if current line already has correct indentation
            if (std::cmp_less(position.line, lines.size()))
            {
                auto const& currentLine = lines[static_cast<size_t>(position.line)];
                auto const existingIndent = getIndent(currentLine);
                if (existingIndent == newIndent)
                    return {};
            }

            return { TextEdit {
                .range =
                    Range {
                        .start = Position { .line = position.line, .character = 0 },
                        .end = Position { .line = position.line, .character = position.character },
                    },
                .newText = newIndent,
            } };
        }
    }
    else if (ch == "|")
    {
        // Align with previous match arm
        if (position.line <= 0)
            return {};

        // Search backwards for a previous '|' at the start of a line (match arm)
        for (auto i = position.line - 1; i >= 0; --i)
        {
            auto const& line = lines[static_cast<size_t>(i)];
            auto const trimmed = trimRight(line);
            auto const indent = getIndent(line);
            auto const content = trimmed.substr(indent.size());

            if (content.starts_with("|") || content.starts_with("match "))
            {
                auto targetIndent = indent;
                if (content.starts_with("match "))
                {
                    // Indent one level from the match keyword
                    targetIndent = indent + "    ";
                }

                // Replace indentation up to the pipe
                auto const currentChar = position.character - 1; // cursor is after '|'
                if (currentChar > 0)
                {
                    return { TextEdit {
                        .range =
                            Range {
                                .start = Position { .line = position.line, .character = 0 },
                                .end = Position { .line = position.line, .character = currentChar },
                            },
                        .newText = targetIndent,
                    } };
                }
                break;
            }
        }
    }

    return {};
}

} // namespace endo::lsp
