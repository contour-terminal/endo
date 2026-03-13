// SPDX-License-Identifier: Apache-2.0
#include "SelectionRangeProvider.hpp"

#include <endo-language/parser/Parser.hpp>

#include <editor-protocol/StubRuntime.hpp>

#include <algorithm>
#include <ranges>
#include <string>
#include <vector>

#include "AstWalker.hpp"

namespace endo::lsp
{

namespace
{

    /// Checks if a position is contained within a source location range (inclusive).
    [[nodiscard]] bool containsPosition(SourceLocationRange const& loc, Position pos)
    {
        // Before start?
        if (pos.line < loc.begin.line || (pos.line == loc.begin.line && pos.character < loc.begin.column))
            return false;
        // After end?
        if (pos.line > loc.end.line || (pos.line == loc.end.line && pos.character > loc.end.column))
            return false;
        return true;
    }

    /// Computes the "size" of a range for sorting (smaller = more specific).
    [[nodiscard]] int rangeSize(SourceLocationRange const& loc)
    {
        auto const lines = loc.end.line - loc.begin.line;
        auto const cols = (lines == 0) ? (loc.end.column - loc.begin.column) : loc.end.column;
        return (lines * 10000) + cols;
    }

    /// Collected node range for selection range building.
    struct NodeRange
    {
        SourceLocationRange location;
    };

    /// Builds a linked SelectionRange chain from a sorted (smallest-first) vector of ranges.
    [[nodiscard]] SelectionRange buildChain(std::vector<NodeRange> const& sorted)
    {
        if (sorted.empty())
            return SelectionRange { .range = Range {}, .parent = nullptr };

        // Start from the outermost (last) and build inward
        std::unique_ptr<SelectionRange> current;
        for (auto const& entry: std::ranges::reverse_view(sorted))
        {
            auto node = std::make_unique<SelectionRange>();
            node->range = toRange(entry.location);
            node->parent = std::move(current);
            current = std::move(node);
        }

        // Move the innermost out of the unique_ptr
        auto result = std::move(*current);
        return result;
    }

} // namespace

std::vector<SelectionRange> computeSelectionRanges(std::string const& source,
                                                   std::vector<Position> const& positions)
{
    // Parse source into AST
    CoreVM::Runtime runtime;
    endo::registerStubRuntime(runtime);

    CoreVM::diagnostics::BufferedReport report;
    Parser parser(runtime, report, std::make_unique<StringSource>(source));
    auto astRoot = parser.parse();
    if (!astRoot)
        return {};

    std::vector<SelectionRange> results;
    results.reserve(positions.size());

    for (auto const& cursor: positions)
    {
        auto ranges = std::vector<NodeRange> {};
        walkStatement(*astRoot, [&](std::optional<SourceLocationRange> const& loc) {
            if (loc && containsPosition(*loc, cursor))
                ranges.push_back(NodeRange { .location = *loc });
        });

        // Sort by range size (smallest first), deduplicate identical ranges
        std::ranges::sort(ranges, [](auto const& a, auto const& b) {
            return rangeSize(a.location) < rangeSize(b.location);
        });

        // Deduplicate identical ranges
        auto last = std::ranges::unique(ranges, [](auto const& a, auto const& b) {
            return a.location.begin.line == b.location.begin.line
                   && a.location.begin.column == b.location.begin.column
                   && a.location.end.line == b.location.end.line
                   && a.location.end.column == b.location.end.column;
        });
        ranges.erase(last.begin(), last.end());

        results.push_back(buildChain(ranges));
    }

    return results;
}

} // namespace endo::lsp
