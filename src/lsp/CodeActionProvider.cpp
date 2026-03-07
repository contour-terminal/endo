// SPDX-License-Identifier: Apache-2.0
#include "CodeActionProvider.hpp"

#include <string>
#include <vector>

namespace endo::lsp
{

namespace
{

    /// Checks if two ranges overlap.
    [[nodiscard]] bool rangesOverlap(Range const& a, Range const& b)
    {
        // a ends before b starts?
        if (a.end.line < b.start.line || (a.end.line == b.start.line && a.end.character < b.start.character))
            return false;
        // b ends before a starts?
        if (b.end.line < a.start.line || (b.end.line == a.start.line && b.end.character < a.start.character))
            return false;
        return true;
    }

} // namespace

std::vector<CodeAction> computeCodeActions(std::string const& /*source*/,
                                           std::string const& uri,
                                           Range range,
                                           std::vector<Diagnostic> const& diagnostics)
{
    std::vector<CodeAction> actions;

    for (auto const& diag: diagnostics)
    {
        // Only process diagnostics that overlap the requested range
        if (!rangesOverlap(diag.range, range))
            continue;

        // Extract suggestions from the data field
        if (!diag.data.has_value() || !diag.data->is_array())
            continue;

        for (auto const& suggestion: *diag.data)
        {
            if (!suggestion.is_string())
                continue;

            auto const text = suggestion.get<std::string>();
            if (text.empty())
                continue;

            auto action = CodeAction {
                .title = text,
                .kind = "quickfix",
                .diagnostics = { diag },
                .isPreferred = false,
            };

            // Pattern: "Did you mean '<name>'?" — replace the misspelled identifier
            if (text.starts_with("Did you mean '") && text.ends_with("'?"))
            {
                auto const nameStart = 14;            // length of "Did you mean '"
                auto const nameEnd = text.size() - 2; // before "'?"
                auto const replacement = text.substr(nameStart, nameEnd - nameStart);

                auto edit = WorkspaceEdit {};
                edit.changes[uri] = { TextEdit {
                    .range = diag.range,
                    .newText = replacement,
                } };
                action.edit = std::move(edit);
                action.isPreferred = true;
            }

            actions.push_back(std::move(action));
        }
    }

    return actions;
}

} // namespace endo::lsp
