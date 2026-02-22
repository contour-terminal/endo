// SPDX-License-Identifier: Apache-2.0
#include "RichConsoleReport.hpp"
#include <shell/ui/SyntaxHighlighter.hpp>

#include <tui/Theme.hpp>

#include <cstdlib>
#include <format>
#include <iostream>
#include <string>
#include <string_view>

#ifdef _WIN32
    #include <io.h>
    #define isatty    _isatty
    #define STDERR_FD 2
#else
    #include <unistd.h>
    #define STDERR_FD STDERR_FILENO
#endif

using CoreVM::diagnostics::Message;
using CoreVM::diagnostics::Type;

namespace endo
{

namespace
{

    /// Returns the diagnostic type label string.
    [[nodiscard]] std::string_view typeLabel(Type type) noexcept
    {
        switch (type)
        {
            case Type::TokenError: return "error[token]";
            case Type::SyntaxError: return "error[syntax]";
            case Type::TypeError: return "error[type]";
            case Type::Warning: return "warning";
            case Type::LinkError: return "error[link]";
        }
        return "error";
    }

    /// Returns true if the message type is a warning.
    [[nodiscard]] bool isWarning(Type type) noexcept
    {
        return type == Type::Warning;
    }

    /// Appends an SGR foreground color escape sequence.
    void appendFg(std::string& out, tui::RgbColor const& color)
    {
        out += std::format("\033[38;2;{};{};{}m", color.r, color.g, color.b);
    }

    /// Appends SGR bold escape sequence.
    void appendBold(std::string& out)
    {
        out += "\033[1m";
    }

    /// Appends SGR reset escape sequence.
    void appendReset(std::string& out)
    {
        out += "\033[m";
    }

    /// Appends a curly underline with the given color.
    /// Uses SGR 4:3 (curly underline style) and SGR 58:2:R:G:B (underline color).
    void appendCurlyUnderline(std::string& out, tui::RgbColor const& color)
    {
        out += "\033[1;4:3m";
        out += std::format("\033[58:2:{}:{}:{}m", color.r, color.g, color.b);
    }

    /// Formats a syntax-highlighted source line using per-grapheme coloring.
    void appendHighlightedSource(std::string& out,
                                 std::string_view source,
                                 tui::Theme const& theme,
                                 bool useColor)
    {
        if (!useColor)
        {
            out += source;
            return;
        }

        auto const highlightMap = computeHighlightMap(source);

        // Walk grapheme clusters in parallel with the highlight map.
        // For ASCII input (the common case), grapheme index == byte index.
        size_t graphemeIdx = 0;
        size_t byteIdx = 0;
        while (byteIdx < source.size() && graphemeIdx < highlightMap.size())
        {
            // Determine grapheme cluster length (simple: assume single byte for ASCII,
            // multi-byte for leading UTF-8 bytes).
            size_t clusterLen = 1;
            if (static_cast<unsigned char>(source[byteIdx]) >= 0x80)
            {
                // UTF-8 multi-byte: count continuation bytes
                auto const lead = static_cast<unsigned char>(source[byteIdx]);
                if ((lead & 0xE0) == 0xC0)
                    clusterLen = 2;
                else if ((lead & 0xF0) == 0xE0)
                    clusterLen = 3;
                else if ((lead & 0xF8) == 0xF0)
                    clusterLen = 4;
            }

            auto const color = categoryColor(highlightMap[graphemeIdx], theme);
            appendFg(out, color);
            out += source.substr(byteIdx, clusterLen);
            appendReset(out);

            byteIdx += clusterLen;
            ++graphemeIdx;
        }

        // Append any remaining bytes (shouldn't normally happen)
        if (byteIdx < source.size())
            out += source.substr(byteIdx);
    }

} // namespace

std::string formatDiagnostic(Message const& message, bool useColor)
{
    auto const& loc = message.sourceLocation;
    auto const& theme = tui::currentTheme();
    auto const label = typeLabel(message.type);
    auto const warning = isWarning(message.type);
    auto const& semanticColor = warning ? theme.colors.warning : theme.colors.error;
    auto const& mutedColor = theme.colors.textMuted;
    auto const& infoColor = theme.colors.info;

    std::string out;

    // --- Error/warning label line ---
    if (useColor)
    {
        appendBold(out);
        appendFg(out, semanticColor);
    }
    out += label;
    out += ": ";
    if (useColor)
        appendReset(out);

    if (useColor)
        appendBold(out);
    out += message.text;
    if (useColor)
        appendReset(out);

    // --- Location line ---
    if (!loc.filename.empty())
    {
        out += '\n';
        if (useColor)
            appendFg(out, mutedColor);
        out += std::format(" --> {}:{}:{}", loc.filename, loc.begin.line, loc.begin.column);
        if (useColor)
            appendReset(out);
    }

    // --- Source context with caret ---
    if (message.contextSnippet.has_value())
    {
        auto const& snippet = message.contextSnippet.value();
        auto const lineNum = loc.begin.line;
        auto const lineNumStr = std::to_string(lineNum);
        auto const gutterWidth = lineNumStr.size();

        // Empty gutter line
        out += '\n';
        if (useColor)
            appendFg(out, mutedColor);
        out += std::string(gutterWidth + 1, ' ');
        out += '|';
        if (useColor)
            appendReset(out);

        // Source line with line number
        out += '\n';
        if (useColor)
            appendFg(out, mutedColor);
        out += lineNumStr;
        out += " | ";
        if (useColor)
            appendReset(out);

        appendHighlightedSource(out, snippet, theme, useColor);

        // Caret/underline line
        if (loc.begin.column > 0)
        {
            auto const column = static_cast<size_t>(loc.begin.column) - 1; // Convert to 0-based
            auto const length =
                (loc.end.column > loc.begin.column) ? (loc.end.column - loc.begin.column) : size_t { 1 };

            out += '\n';
            if (useColor)
                appendFg(out, mutedColor);
            out += std::string(gutterWidth + 1, ' ');
            out += "| ";
            if (useColor)
                appendReset(out);

            // Spaces before the underline
            out += std::string(column, ' ');

            // Curly underline
            if (useColor)
                appendCurlyUnderline(out, theme.colors.warning);

            out += std::string(length, '~');

            if (useColor)
                appendReset(out);
        }

        // Closing gutter line (before hints or at end)
        out += '\n';
        if (useColor)
            appendFg(out, mutedColor);
        out += std::string(gutterWidth + 1, ' ');
        out += '|';
        if (useColor)
            appendReset(out);
    }

    // --- Hints/suggestions ---
    for (auto const& suggestion: message.suggestions)
    {
        out += '\n';
        if (useColor)
            appendFg(out, mutedColor);
        out += "  = ";
        if (useColor)
            appendReset(out);

        if (useColor)
        {
            appendBold(out);
            appendFg(out, infoColor);
        }
        out += "hint";
        if (useColor)
            appendReset(out);

        out += ": ";
        out += suggestion;
    }

    return out;
}

RichConsoleReport::RichConsoleReport()
{
    auto const* noColor = std::getenv("NO_COLOR");
    _useColor = isatty(STDERR_FD) && (noColor == nullptr || noColor[0] == '\0');
}

void RichConsoleReport::push_back(CoreVM::diagnostics::Message message)
{
    if (!isWarning(message.type))
        _errorCount++;

    std::cerr << formatDiagnostic(message, _useColor) << '\n';
}

bool RichConsoleReport::containsFailures() const noexcept
{
    return _errorCount > 0;
}

} // namespace endo
