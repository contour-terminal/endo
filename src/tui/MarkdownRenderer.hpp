// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tui/GenericSyntaxHighlighter.hpp>
#include <tui/TerminalOutput.hpp>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tui
{

/// @brief Visual style for table rendering.
enum class TableRenderStyle : std::uint8_t
{
    Bordered, ///< Box-drawing borders (default).
    Compact,  ///< Bold header + underline separator, space-separated columns, no vertical borders.
    Plain,    ///< No borders or emphasis — plain text only.
};

/// @brief Optional callback for per-cell styling in table rendering.
///
/// Returns a custom Style for the cell at (row, col), or nullopt for the default.
/// @p row is 0-based data row index (headers are not passed through the callback).
using CellStyleFn = std::function<std::optional<Style>(size_t row, size_t col, std::string_view text)>;

/// @brief Theme for markdown rendering with terminal styles.
struct MarkdownTheme
{
    Style heading1;        ///< H1: bold + double-height.
    Style heading2;        ///< H2: bold + underline.
    Style heading3;        ///< H3: bold.
    Style headingEmphasis; ///< **bold** text within headings (distinct color to raise attention).
    Style codeBlock;       ///< Fenced code block.
    Style codeInline;      ///< Inline `code`.
    Style bold;            ///< **bold** text.
    Style italic;          ///< *italic* text.
    Style link;            ///< [link](url).
    Style listMarker;      ///< List bullet/number.
    Style blockquote;      ///< > quoted text.
    Style thinkBlock;      ///< <think>...</think> content.
    Style tableBorder;     ///< Box-drawing characters for table borders.
    Style tableHeader;     ///< Table header cell text.
    Style tableCell;       ///< Table data cell text.
};

/// @brief Renders markdown to a terminal output with styling.
///
/// Supports both batch rendering of complete markdown strings and streaming
/// rendering for incremental token output (e.g. from an LLM). Handles headings,
/// code blocks, inline code, bold, italic, links, lists, blockquotes, and
/// <think>...</think> blocks.
class MarkdownRenderer
{
  public:
    /// @brief Constructs a renderer targeting the given terminal output.
    /// @param output The terminal output to render to.
    /// @param theme The styling theme (defaults to defaultTheme()).
    explicit MarkdownRenderer(TerminalOutput& output, MarkdownTheme theme = defaultTheme());

    /// @brief Renders a complete markdown string.
    /// @param markdown The markdown text to render.
    void render(std::string_view markdown);

    /// @brief Begins a streaming rendering session.
    void beginStream();

    /// @brief Feeds a token (partial text) for incremental rendering.
    /// @param token The next chunk of text from the LLM.
    void feedToken(std::string_view token);

    /// @brief Ends the streaming session and flushes remaining buffered content.
    void endStream();

    /// @brief Enables full-width mode for DEC line attribute headings.
    ///
    /// When enabled, H1 headings use double-height/double-width (ESC#3/ESC#4)
    /// and H2 headings use double-width (ESC#6). Only works correctly when
    /// rendering to the full terminal width.
    void setFullWidthMode(bool enabled) noexcept;

    /// @brief Sets the maximum width for table rendering with word wrapping.
    ///
    /// When set to a positive value, table columns are constrained to fit within
    /// this width, and cell text is word-wrapped as needed. A value of 0 disables
    /// width constraining (default).
    /// @param width Maximum table width in columns (0 = unconstrained).
    void setMaxWidth(int width) noexcept;

    /// @brief Sets the visual style for table rendering.
    /// @param style The desired table render style.
    void setTableRenderStyle(TableRenderStyle style) noexcept;

    /// @brief Sets a per-cell style callback for custom table cell coloring.
    /// @param fn The callback, or nullptr to clear.
    void setCellStyleCallback(CellStyleFn fn);

    /// @brief Returns the default theme with sensible terminal colors.
    [[nodiscard]] static auto defaultTheme() -> MarkdownTheme;

  private:
    TerminalOutput& _output;
    MarkdownTheme _theme;

    // Streaming state
    std::string _streamBuffer;
    bool _streaming = false;
    bool _fullWidthMode = false;
    int _maxWidth = 0; ///< Maximum width for table rendering (0 = unconstrained).
    TableRenderStyle _tableRenderStyle = TableRenderStyle::Bordered; ///< Visual table style.
    CellStyleFn _cellStyleFn;                                        ///< Optional per-cell style callback.
    bool _inCodeBlock = false;
    bool _inThinkBlock = false;
    std::string _codeFence; ///< The fence string (e.g. "```") that opened the current code block.
    LanguageId _codeLanguage = LanguageId::None;                 ///< Detected language of current code block.
    HighlightState _codeHighlightState = HighlightState::Normal; ///< Multi-line highlight state.

    // Table buffering state
    bool _inTable = false;                ///< Currently buffering table rows.
    std::vector<std::string> _tableLines; ///< Buffered table lines.
    bool _tableSeparatorSeen = false;     ///< Separator row detected in buffered lines.

    /// @brief Renders a single line of markdown (not inside a code block).
    void renderLine(std::string_view line);

    /// @brief Renders inline markdown elements (bold, italic, code, links).
    /// @param text The inline text to render.
    /// @param baseStyle Optional style for plain text (nullptr = unstyled raw output).
    /// @param boldStyle Optional style override for **bold** text (nullptr = theme default).
    void renderInline(std::string_view text,
                      Style const* baseStyle = nullptr,
                      Style const* boldStyle = nullptr);

    /// @brief Processes buffered streaming content, rendering complete lines.
    void processStreamBuffer();

    /// @brief Handles a potential table line (buffers or flushes).
    /// @param line The line to check.
    /// @return true if the line was consumed as part of a table.
    auto handleTableLine(std::string_view line) -> bool;

    /// @brief Flushes buffered table lines, rendering or falling back to paragraphs.
    void flushTable();

    /// @brief Renders a fully parsed table with box-drawing borders.
    /// @param table The parsed table to render.
    void renderTable(struct ParsedTable const& table);

    /// @brief Renders a table in compact style (bold header + underline, no vertical borders).
    /// @param table The parsed table to render.
    /// @param showHeader Whether to bold the header and render an underline separator.
    void renderTableCompact(struct ParsedTable const& table, bool showHeader);

    /// @brief Renders a heading line.
    /// @param level The heading level (1-6).
    /// @param text The heading text.
    void renderHeading(int level, std::string_view text);
};

} // namespace tui
