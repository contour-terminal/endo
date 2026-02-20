// SPDX-License-Identifier: Apache-2.0
#include "HoverProvider.hpp"

#include <endo-language/ide/HoverProvider.hpp>

namespace endo::lsp
{

std::optional<Hover> computeHover(std::string const& source, Position position)
{
    auto result =
        endo::computeHover(source, SourcePosition { .line = position.line, .character = position.character });
    if (!result)
        return std::nullopt;

    auto hover = Hover {
        .contents = MarkupContent { .value = std::move(result->markdownText) },
    };

    if (result->range)
    {
        hover.range = Range {
            .start =
                Position { .line = result->range->start.line, .character = result->range->start.character },
            .end = Position { .line = result->range->end.line, .character = result->range->end.character },
        };
    }

    return hover;
}

} // namespace endo::lsp
