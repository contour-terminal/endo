// SPDX-License-Identifier: Apache-2.0
#include "DiagnosticsProvider.hpp"

#include <endo-language/Lexer.hpp>
#include <endo-language/Parser.hpp>

namespace endo::lsp
{

std::vector<Diagnostic> computeDiagnostics(std::string const& source,
                                           std::string_view fileName,
                                           CoreVM::Runtime& runtime)
{
    CoreVM::diagnostics::BufferedReport report;
    Parser parser(runtime, report, std::make_unique<StringSource>(source));
    parser.parse();

    std::vector<Diagnostic> diagnostics;
    for (auto const& msg: report.messages())
    {
        auto severity = DiagnosticSeverity::Error;
        using Type = CoreVM::diagnostics::Type;
        switch (msg.type)
        {
            case Type::Warning: severity = DiagnosticSeverity::Warning; break;
            case Type::LinkError: [[fallthrough]];
            case Type::TypeError: severity = DiagnosticSeverity::Error; break;
            default: break;
        }

        // CoreVM FilePos uses 1-based line/column; LSP uses 0-based
        auto const startLine =
            msg.sourceLocation.begin.line > 0 ? static_cast<int>(msg.sourceLocation.begin.line) - 1 : 0;
        auto const startCol =
            msg.sourceLocation.begin.column > 0 ? static_cast<int>(msg.sourceLocation.begin.column) - 1 : 0;
        auto const endLine =
            msg.sourceLocation.end.line > 0 ? static_cast<int>(msg.sourceLocation.end.line) - 1 : startLine;
        auto const endCol = msg.sourceLocation.end.column > 0
                                ? static_cast<int>(msg.sourceLocation.end.column) - 1
                                : startCol + 1;

        diagnostics.push_back(Diagnostic {
            .range =
                Range {
                    .start = Position { .line = startLine, .character = startCol },
                    .end = Position { .line = endLine, .character = endCol },
                },
            .severity = severity,
            .source = "endo",
            .message = msg.text,
        });
    }

    return diagnostics;
}

} // namespace endo::lsp
