// SPDX-License-Identifier: Apache-2.0
#include "DiagnosticsProvider.hpp"

#include <endo-language/DiagnosticsCollector.hpp>

namespace endo::lsp
{

std::vector<Diagnostic> computeDiagnostics(std::string const& source,
                                           std::string_view /*fileName*/,
                                           CoreVM::Runtime& /*runtime*/)
{
    auto const messages = endo::collectDiagnostics(source);

    std::vector<Diagnostic> diagnostics;
    diagnostics.reserve(messages.size());

    for (auto const& msg: messages)
    {
        diagnostics.push_back(Diagnostic {
            .range =
                Range {
                    .start =
                        Position { .line = msg.range.start.line, .character = msg.range.start.character },
                    .end = Position { .line = msg.range.end.line, .character = msg.range.end.character },
                },
            .severity = static_cast<DiagnosticSeverity>(static_cast<int>(msg.severity)),
            .source = "endo",
            .message = msg.message,
        });
    }

    return diagnostics;
}

} // namespace endo::lsp
