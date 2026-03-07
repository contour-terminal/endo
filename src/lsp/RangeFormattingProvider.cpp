// SPDX-License-Identifier: Apache-2.0
#include "RangeFormattingProvider.hpp"

#include <endo-language/format/SourceFormatter.hpp>

#include <algorithm>
#include <sstream>
#include <string>

namespace endo::lsp
{

namespace
{
    /// Splits source into individual lines (preserving line endings).
    [[nodiscard]] std::vector<std::string> splitLines(std::string const& text)
    {
        std::vector<std::string> lines;
        std::istringstream stream(text);
        std::string line;
        while (std::getline(stream, line))
            lines.push_back(line);
        if (lines.empty())
            lines.emplace_back();
        return lines;
    }
} // namespace

std::vector<TextEdit> computeRangeFormatting(std::string const& source, Range range)
{
    auto const formatted = format::SourceFormatter::format(source);
    if (formatted == source)
        return {};

    auto const originalLines = splitLines(source);
    auto const formattedLines = splitLines(formatted);

    auto const startLine = std::max(0, range.start.line);
    auto const endLine = std::min(static_cast<int>(originalLines.size()) - 1, range.end.line);

    std::vector<TextEdit> edits;
    auto const maxLine = std::min(endLine, static_cast<int>(formattedLines.size()) - 1);

    for (auto line = startLine; line <= maxLine; ++line)
    {
        auto const origLine = (std::cmp_less(line, originalLines.size()))
                                  ? originalLines[static_cast<size_t>(line)]
                                  : std::string {};
        auto const fmtLine = (std::cmp_less(line, formattedLines.size()))
                                 ? formattedLines[static_cast<size_t>(line)]
                                 : std::string {};

        if (origLine != fmtLine)
        {
            edits.push_back(TextEdit {
                .range =
                    Range {
                        .start = Position { .line = line, .character = 0 },
                        .end = Position { .line = line, .character = static_cast<int>(origLine.size()) },
                    },
                .newText = fmtLine,
            });
        }
    }

    return edits;
}

} // namespace endo::lsp
