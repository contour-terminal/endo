// SPDX-License-Identifier: Apache-2.0
#include "DiagnosticsProvider.hpp"

#include <endo-language/ide/DiagnosticsCollector.hpp>

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
        auto fullMessage = msg.message;
        for (auto const& hint: msg.suggestions)
            fullMessage += "\nhint: " + hint;

        diagnostics.push_back(Diagnostic {
            .range =
                Range {
                    .start =
                        Position { .line = msg.range.start.line, .character = msg.range.start.character },
                    .end = Position { .line = msg.range.end.line, .character = msg.range.end.character },
                },
            .severity = static_cast<DiagnosticSeverity>(static_cast<int>(msg.severity)),
            .source = "endo",
            .message = fullMessage,
        });
    }

    return diagnostics;
}

} // namespace endo::lsp
