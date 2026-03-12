// SPDX-License-Identifier: Apache-2.0
#include "FoldingRangeProvider.hpp"

#include <endo-language/lexer/Lexer.hpp>
#include <endo-language/parser/Parser.hpp>

#include <editor-protocol/StubRuntime.hpp>

#include <string>
#include <vector>

#include "AstWalker.hpp"

namespace endo::lsp
{

namespace
{

    /// Checks if a source location range spans multiple lines.
    [[nodiscard]] bool isMultiLine(SourceLocationRange const& loc)
    {
        return loc.begin.line < loc.end.line;
    }

    /// Creates a FoldingRange from a SourceLocationRange with the given kind.
    [[nodiscard]] FoldingRange makeFold(SourceLocationRange const& loc,
                                        std::optional<std::string> kind = "region")
    {
        return FoldingRange {
            .startLine = loc.begin.line,
            .startCharacter = loc.begin.column,
            .endLine = loc.end.line,
            .endCharacter = loc.end.column,
            .kind = std::move(kind),
        };
    }

} // namespace

std::vector<FoldingRange> computeFoldingRanges(std::string const& source)
{
    // Parse source into AST
    CoreVM::Runtime runtime;
    endo::registerStubRuntime(runtime);

    CoreVM::diagnostics::BufferedReport report;
    Parser parser(runtime, report, std::make_unique<StringSource>(source));
    auto astRoot = parser.parse();
    if (!astRoot)
        return {};

    // Walk AST to collect folding ranges
    auto ranges = std::vector<FoldingRange> {};
    walkStatement(*astRoot, [&](std::optional<SourceLocationRange> const& loc) {
        if (loc && isMultiLine(*loc))
            ranges.push_back(makeFold(*loc));
    });

    // Collect multi-line comments
    auto lexer = Lexer { std::make_unique<StringSource>(source), true };
    while (lexer.currentToken() != Token::EndOfInput)
        lexer.nextToken();

    for (auto const& comment: lexer.comments())
    {
        if (isMultiLine(comment.location))
            ranges.push_back(makeFold(comment.location, "comment"));
    }

    return ranges;
}

} // namespace endo::lsp
