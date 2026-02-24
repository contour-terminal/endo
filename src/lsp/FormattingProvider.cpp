// SPDX-License-Identifier: Apache-2.0
#include "FormattingProvider.hpp"

#include <endo-language/format/SourceFormatter.hpp>

#include <algorithm>
#include <string>

namespace endo::lsp
{

std::vector<TextEdit> computeFormatting(std::string const& source)
{
    auto const formatted = format::SourceFormatter::format(source);

    // No changes needed
    if (formatted == source)
        return {};

    // Count lines in original source to determine the full document range
    auto const lineCount = static_cast<int>(std::ranges::count(source, '\n') + 1);
    auto const lastLineLength = [&]() -> int {
        auto const lastNewline = source.rfind('\n');
        if (lastNewline == std::string::npos)
            return static_cast<int>(source.size());
        return static_cast<int>(source.size() - lastNewline - 1);
    }();

    // Single edit replacing the entire document
    return { TextEdit {
        .range =
            Range {
                .start = Position { .line = 0, .character = 0 },
                .end = Position { .line = lineCount - 1, .character = lastLineLength },
            },
        .newText = formatted,
    } };
}

} // namespace endo::lsp
