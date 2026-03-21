// SPDX-License-Identifier: Apache-2.0
#include "RichConsoleReport.hpp"
#include <shell/TTY.hpp>
#include <shell/ui/SyntaxHighlighter.hpp>

#include <endo-language/parser/DiagnosticsAdapter.hpp>

#include <tui/Theme.hpp>

#include <cstdlib>
#include <format>
#include <iostream>
#include <string>
#include <string_view>

#include <platform/Types.hpp>

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
    /// When an underline span is provided (underlineLength > 0), applies curly underline
    /// directly to the source characters within the span instead of using a separate caret line.
    void appendHighlightedSource(std::string& out,
                                 std::string_view source,
                                 tui::Theme const& theme,
                                 bool useColor,
                                 size_t underlineColumn = 0,
                                 size_t underlineLength = 0,
                                 tui::RgbColor const* underlineColor = nullptr)
    {
        if (!useColor)
        {
            out += source;
            return;
        }

        auto const highlightMap = computeHighlightMap(source);
        auto const underlineEnd = underlineColumn + underlineLength;

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

            auto const inUnderlineSpan = underlineColor && underlineLength > 0
                                         && graphemeIdx >= underlineColumn && graphemeIdx < underlineEnd;

            auto const color = categoryColor(highlightMap[graphemeIdx], theme);
            appendFg(out, color);
            if (inUnderlineSpan)
                appendCurlyUnderline(out, *underlineColor);
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

        // Compute underline span (0-based column and length)
        auto const column = loc.begin.column > 0 ? static_cast<size_t>(loc.begin.column) - 1 : size_t { 0 };
        auto const length =
            (loc.end.column > loc.begin.column) ? (loc.end.column - loc.begin.column) : size_t { 1 };

        if (useColor)
        {
            // Apply curly underline directly on the source characters
            appendHighlightedSource(out, snippet, theme, useColor, column, length, &semanticColor);
        }
        else
        {
            appendHighlightedSource(out, snippet, theme, useColor);

            // Plain text fallback: separate caret/underline line
            if (loc.begin.column > 0)
            {
                out += '\n';
                out += std::string(gutterWidth + 1, ' ');
                out += "| ";
                out += std::string(column, ' ');
                out += std::string(length, '~');
            }
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
    _useColor = endo::isTerminal(endo::standardError()) && (noColor == nullptr || noColor[0] == '\0');
}

RichConsoleReport::RichConsoleReport(TTY const& tty): _tty(&tty)
{
    auto const* noColor = std::getenv("NO_COLOR");
    _useColor = tty.isStderrTerminal() && (noColor == nullptr || noColor[0] == '\0');
}

void RichConsoleReport::setSourceText(std::string_view source)
{
    _sourceText = source;
}

void RichConsoleReport::push_back(CoreVM::diagnostics::Message message)
{
    if (!isWarning(message.type))
        _errorCount++;

    // Fill in missing context snippet from source text if available.
    // The parser and IRGenerator don't always provide contextSnippet,
    // but we can extract the relevant line from the source text ourselves.
    if (!message.contextSnippet.has_value() && !_sourceText.empty() && message.sourceLocation.begin.line > 0)
    {
        // SourceLocation uses 1-based lines; extractSourceLine uses 0-based.
        auto const line =
            extractSourceLine(_sourceText, static_cast<int>(message.sourceLocation.begin.line) - 1);
        if (!line.empty())
            message.contextSnippet = line;
    }

    auto formatted = formatDiagnostic(message, _useColor) + '\n';
    if (_tty)
        _tty->writeToStderr(formatted);
    else
        std::cerr << formatted;
}

bool RichConsoleReport::containsFailures() const noexcept
{
    return _errorCount > 0;
}

// =================================================================================================
// BufferingConsoleReport
// =================================================================================================

BufferingConsoleReport::BufferingConsoleReport(): BufferingConsoleReport(ColorMode::Auto)
{
}

BufferingConsoleReport::BufferingConsoleReport(ColorMode colorMode)
{
    switch (colorMode)
    {
        case ColorMode::Enabled: _useColor = true; break;
        case ColorMode::Disabled: _useColor = false; break;
        case ColorMode::Auto: {
            auto const* noColor = std::getenv("NO_COLOR");
            _useColor = endo::isTerminal(endo::standardError()) && (noColor == nullptr || noColor[0] == '\0');
            break;
        }
    }
}

void BufferingConsoleReport::setSourceText(std::string_view source)
{
    _sourceText = source;
}

void BufferingConsoleReport::push_back(CoreVM::diagnostics::Message message)
{
    if (!isWarning(message.type))
        _errorCount++;

    // Fill in missing context snippet from source text if available.
    if (!message.contextSnippet.has_value() && !_sourceText.empty() && message.sourceLocation.begin.line > 0)
    {
        auto const line =
            extractSourceLine(_sourceText, static_cast<int>(message.sourceLocation.begin.line) - 1);
        if (!line.empty())
            message.contextSnippet = line;
    }

    _formattedMessages.push_back(formatDiagnostic(message, _useColor));
}

bool BufferingConsoleReport::containsFailures() const noexcept
{
    return _errorCount > 0;
}

std::vector<std::string> const& BufferingConsoleReport::formattedMessages() const noexcept
{
    return _formattedMessages;
}

bool BufferingConsoleReport::hasMessages() const noexcept
{
    return !_formattedMessages.empty();
}

} // namespace endo
