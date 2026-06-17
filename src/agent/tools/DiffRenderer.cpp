// SPDX-License-Identifier: Apache-2.0
#include <tui/GenericSyntaxHighlighter.hpp>
#include <tui/Theme.hpp>

#include <algorithm>
#include <format>
#include <string>
#include <vector>

#include <agent/tools/DiffRenderer.hpp>

namespace endo::agent
{

namespace
{

    /// @brief Splits a string into lines, preserving empty trailing lines from a final newline.
    [[nodiscard]] auto splitLines(std::string_view text) -> std::vector<std::string>
    {
        auto lines = std::vector<std::string> {};
        if (text.empty())
            return lines;

        auto start = std::string_view::size_type { 0 };
        while (start < text.size())
        {
            auto const pos = text.find('\n', start);
            if (pos == std::string_view::npos)
            {
                lines.emplace_back(text.substr(start));
                break;
            }
            lines.emplace_back(text.substr(start, pos - start));
            start = pos + 1;
        }
        // A trailing newline produces an empty trailing element only if text ends with '\n'.
        if (!text.empty() && text.back() == '\n')
            lines.emplace_back();
        return lines;
    }

    /// @brief Represents an edit operation in the LCS diff.
    enum class EditOp : std::uint8_t
    {
        Keep,   ///< Line is common to both sequences.
        Insert, ///< Line was added in new text.
        Delete, ///< Line was removed from old text.
    };

    /// @brief Computes the edit script between two line sequences using LCS.
    ///
    /// Uses the classic O(n*m) dynamic programming approach. The strings from
    /// edit_file are typically small (tens of lines), so this is fast enough.
    [[nodiscard]] auto computeEditScript(std::span<std::string const> oldLines,
                                         std::span<std::string const> newLines) -> std::vector<EditOp>
    {
        auto const n = oldLines.size();
        auto const m = newLines.size();

        // Build LCS table.
        auto dp = std::vector<std::vector<int>>(n + 1, std::vector<int>(m + 1, 0));
        for (auto i = std::size_t { 1 }; i <= n; ++i)
            for (auto j = std::size_t { 1 }; j <= m; ++j)
            {
                if (oldLines[i - 1] == newLines[j - 1])
                    dp[i][j] = dp[i - 1][j - 1] + 1;
                else
                    dp[i][j] = std::max(dp[i - 1][j], dp[i][j - 1]);
            }

        // Backtrack to produce edit script.
        auto ops = std::vector<EditOp> {};
        auto i = n;
        auto j = m;
        while (i > 0 || j > 0)
        {
            if (i > 0 && j > 0 && oldLines[i - 1] == newLines[j - 1])
            {
                ops.push_back(EditOp::Keep);
                --i;
                --j;
            }
            else if (j > 0 && (i == 0 || dp[i][j - 1] >= dp[i - 1][j]))
            {
                ops.push_back(EditOp::Insert);
                --j;
            }
            else
            {
                ops.push_back(EditOp::Delete);
                --i;
            }
        }
        std::ranges::reverse(ops);
        return ops;
    }

} // namespace

auto generateUnifiedDiff(std::string_view oldText, std::string_view newText, int contextLines)
    -> std::vector<DiffLine>
{
    if (oldText == newText)
        return {};

    auto const oldLines = splitLines(oldText);
    auto const newLines = splitLines(newText);
    auto const ops = computeEditScript(oldLines, newLines);

    // Annotate each op with its line numbers and change status.
    struct AnnotatedOp
    {
        EditOp op;
        int oldLine; ///< 1-based line number in old text.
        int newLine; ///< 1-based line number in new text.
    };

    auto annotated = std::vector<AnnotatedOp> {};
    auto oldIdx = 0;
    auto newIdx = 0;
    for (auto const& op: ops)
    {
        switch (op)
        {
            case EditOp::Keep:
                ++oldIdx;
                ++newIdx;
                annotated.push_back({ .op = op, .oldLine = oldIdx, .newLine = newIdx });
                break;
            case EditOp::Delete:
                ++oldIdx;
                annotated.push_back({ .op = op, .oldLine = oldIdx, .newLine = 0 });
                break;
            case EditOp::Insert:
                ++newIdx;
                annotated.push_back({ .op = op, .oldLine = 0, .newLine = newIdx });
                break;
        }
    }

    // Find ranges of changes and expand with context lines to form hunks.
    auto isChange = [](AnnotatedOp const& a) {
        return a.op != EditOp::Keep;
    };
    auto const total = static_cast<int>(annotated.size());

    // Collect hunk boundaries: contiguous change regions expanded by context.
    struct HunkRange
    {
        int start;
        int end; ///< Exclusive.
    };

    auto hunks = std::vector<HunkRange> {};

    for (auto i = 0; i < total; ++i)
    {
        if (!isChange(annotated[static_cast<std::size_t>(i)]))
            continue;

        auto start = std::max(0, i - contextLines);
        auto end = i + 1;

        // Extend to cover contiguous changes and their context.
        while (end < total)
        {
            if (isChange(annotated[static_cast<std::size_t>(end)]))
            {
                ++end;
                continue;
            }
            // Check if next change is within context distance.
            auto nextChange = end;
            while (nextChange < total && !isChange(annotated[static_cast<std::size_t>(nextChange)]))
                ++nextChange;

            if (nextChange < total && nextChange - end <= contextLines * 2)
            {
                end = nextChange + 1;
                continue;
            }
            break;
        }

        end = std::min(total, end + contextLines);
        i = end - 1; // Skip past this hunk.

        // Merge with previous hunk if overlapping.
        if (!hunks.empty() && start <= hunks.back().end)
            hunks.back().end = end;
        else
            hunks.push_back({ .start = start, .end = end });
    }

    // Build diff lines from hunks.
    auto result = std::vector<DiffLine> {};
    for (auto const& [start, end]: hunks)
    {
        // Compute hunk header line ranges.
        auto const& first = annotated[static_cast<std::size_t>(start)];
        auto const& last = annotated[static_cast<std::size_t>(end - 1)];

        auto oldStart = first.oldLine > 0 ? first.oldLine : first.newLine;
        auto newStart = first.newLine > 0 ? first.newLine : first.oldLine;

        // Count old and new lines in this hunk.
        auto oldCount = 0;
        auto newCount = 0;
        for (auto i = start; i < end; ++i)
        {
            auto const& a = annotated[static_cast<std::size_t>(i)];
            if (a.op == EditOp::Keep || a.op == EditOp::Delete)
                ++oldCount;
            if (a.op == EditOp::Keep || a.op == EditOp::Insert)
                ++newCount;
        }

        // Find proper start line numbers.
        for (auto i = start; i < end; ++i)
        {
            auto const& a = annotated[static_cast<std::size_t>(i)];
            if (a.oldLine > 0)
            {
                oldStart = a.oldLine;
                break;
            }
        }
        for (auto i = start; i < end; ++i)
        {
            auto const& a = annotated[static_cast<std::size_t>(i)];
            if (a.newLine > 0)
            {
                newStart = a.newLine;
                break;
            }
        }

        result.push_back(
            { .type = DiffLineType::Hunk,
              .text = std::format("@@ -{},{} +{},{} @@", oldStart, oldCount, newStart, newCount) });

        for (auto i = start; i < end; ++i)
        {
            auto const& a = annotated[static_cast<std::size_t>(i)];
            switch (a.op)
            {
                case EditOp::Keep:
                    result.push_back({ .type = DiffLineType::Context,
                                       .text = oldLines[static_cast<std::size_t>(a.oldLine - 1)],
                                       .oldLineNum = a.oldLine,
                                       .newLineNum = a.newLine });
                    break;
                case EditOp::Delete:
                    result.push_back({ .type = DiffLineType::Deletion,
                                       .text = oldLines[static_cast<std::size_t>(a.oldLine - 1)],
                                       .oldLineNum = a.oldLine });
                    break;
                case EditOp::Insert:
                    result.push_back({ .type = DiffLineType::Addition,
                                       .text = newLines[static_cast<std::size_t>(a.newLine - 1)],
                                       .newLineNum = a.newLine });
                    break;
            }
        }
    }

    return result;
}

void renderDiff(tui::TerminalOutput& output,
                std::string_view filePath,
                std::span<DiffLine const> diffLines,
                bool truncated)
{
    if (diffLines.empty())
        return;

    auto const& theme = tui::currentTheme();
    auto const barStyle = tui::Style { .fg = theme.agentColors.leftBar };
    auto const addStyle = tui::Style { .fg = theme.colors.success };
    auto const delStyle = tui::Style { .fg = theme.colors.error };
    auto const ctxStyle = tui::Style { .fg = theme.agentColors.statusText };
    auto const hunkStyle = tui::Style { .fg = theme.agentColors.leftBar, .dim = true };
    auto const headerStyle = tui::Style { .fg = theme.agentColors.leftBar, .bold = true };

    // Diff header line.
    output.writeText("\u2502 ", barStyle);
    output.writeText(std::format("\u2500\u2500 {} \u2500\u2500", filePath), headerStyle);
    output.linefeed();

    // Compute the max line number width for alignment.
    auto maxLineNum = 0;
    for (auto const& line: diffLines)
    {
        maxLineNum = std::max(maxLineNum, line.oldLineNum);
        maxLineNum = std::max(maxLineNum, line.newLineNum);
    }
    auto const lineNumWidth = maxLineNum > 0 ? static_cast<int>(std::to_string(maxLineNum).size()) : 1;

    for (auto const& line: diffLines)
    {
        output.writeText("\u2502 ", barStyle);

        switch (line.type)
        {
            case DiffLineType::Hunk:
                output.writeText(std::format("{:>{}}  ", "", lineNumWidth), ctxStyle); // Blank line number.
                output.writeText(line.text, hunkStyle);
                break;
            case DiffLineType::Context:
                output.writeText(std::format("{:>{}}  ", line.oldLineNum, lineNumWidth), ctxStyle);
                output.writeText("  ", ctxStyle);
                output.writeText(line.text, ctxStyle);
                break;
            case DiffLineType::Deletion:
                output.writeText(std::format("{:>{}}  ", line.oldLineNum, lineNumWidth), ctxStyle);
                output.writeText("- ", delStyle);
                output.writeText(line.text, delStyle);
                break;
            case DiffLineType::Addition:
                output.writeText(std::format("{:>{}}  ", line.newLineNum, lineNumWidth), ctxStyle);
                output.writeText("+ ", addStyle);
                output.writeText(line.text, addStyle);
                break;
        }
        output.linefeed();
    }

    if (truncated)
    {
        output.writeText("\u2502 ", barStyle);
        output.writeText("  (diff truncated — large edit)", ctxStyle);
        output.linefeed();
    }
}

void renderDiff(tui::TerminalOutput& output,
                std::string_view filePath,
                std::span<DiffLine const> diffLines,
                tui::LanguageId language,
                bool truncated)
{
    // Fall back to non-highlighted rendering if no language detected
    if (language == tui::LanguageId::None)
    {
        renderDiff(output, filePath, diffLines, truncated);
        return;
    }

    if (diffLines.empty())
        return;

    auto const& theme = tui::currentTheme();
    auto const barStyle = tui::Style { .fg = theme.agentColors.leftBar };
    auto const addStyle = tui::Style { .fg = theme.colors.success };
    auto const delStyle = tui::Style { .fg = theme.colors.error };
    auto const ctxStyle = tui::Style { .fg = theme.agentColors.statusText };
    auto const hunkStyle = tui::Style { .fg = theme.agentColors.leftBar, .dim = true };
    auto const headerStyle = tui::Style { .fg = theme.agentColors.leftBar, .bold = true };

    // Diff header line.
    output.writeText("\u2502 ", barStyle);
    output.writeText(std::format("\u2500\u2500 {} \u2500\u2500", filePath), headerStyle);
    output.linefeed();

    // Compute the max line number width for alignment.
    auto maxLineNum = 0;
    for (auto const& line: diffLines)
    {
        maxLineNum = std::max(maxLineNum, line.oldLineNum);
        maxLineNum = std::max(maxLineNum, line.newLineNum);
    }
    auto const lineNumWidth = maxLineNum > 0 ? static_cast<int>(std::to_string(maxLineNum).size()) : 1;

    // Track highlight state across lines (reset at hunk boundaries)
    auto hlState = tui::HighlightState::Normal;

    for (auto const& line: diffLines)
    {
        output.writeText("\u2502 ", barStyle);

        switch (line.type)
        {
            case DiffLineType::Hunk:
                output.writeText(std::format("{:>{}}  ", "", lineNumWidth), ctxStyle);
                output.writeText(line.text, hunkStyle);
                hlState = tui::HighlightState::Normal; // Reset at hunk boundary
                break;
            case DiffLineType::Context: {
                output.writeText(std::format("{:>{}}  ", line.oldLineNum, lineNumWidth), ctxStyle);
                output.writeText("  ", ctxStyle);
                auto [highlights, newState] = tui::highlightLine(line.text, language, hlState);
                hlState = newState;
                auto dimStyle = tui::Style { .dim = true };
                tui::renderHighlightedLine(output, line.text, highlights, dimStyle, theme);
                break;
            }
            case DiffLineType::Deletion:
                output.writeText(std::format("{:>{}}  ", line.oldLineNum, lineNumWidth), ctxStyle);
                output.writeText("- ", delStyle);
                output.writeText(line.text, delStyle);
                break;
            case DiffLineType::Addition: {
                output.writeText(std::format("{:>{}}  ", line.newLineNum, lineNumWidth), ctxStyle);
                output.writeText("+ ", addStyle);
                auto [highlights, newState] = tui::highlightLine(line.text, language, hlState);
                hlState = newState;
                tui::renderHighlightedLine(output, line.text, highlights, tui::Style {}, theme);
                break;
            }
        }
        output.linefeed();
    }

    if (truncated)
    {
        output.writeText("\u2502 ", barStyle);
        output.writeText("  (diff truncated — large edit)", ctxStyle);
        output.linefeed();
    }
}

} // namespace endo::agent
