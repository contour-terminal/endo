// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tui/TerminalOutput.hpp>

#include <string>
#include <string_view>

namespace tui
{

/// @brief Theme for markdown rendering with terminal styles.
struct MarkdownTheme
{
    Style heading1;   ///< H1: bold + double-height.
    Style heading2;   ///< H2: bold + underline.
    Style heading3;   ///< H3: bold.
    Style codeBlock;  ///< Fenced code block.
    Style codeInline; ///< Inline `code`.
    Style bold;       ///< **bold** text.
    Style italic;     ///< *italic* text.
    Style link;       ///< [link](url).
    Style listMarker; ///< List bullet/number.
    Style blockquote; ///< > quoted text.
    Style thinkBlock; ///< <think>...</think> content.
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

    /// @brief Returns the default theme with sensible terminal colors.
    [[nodiscard]] static auto defaultTheme() -> MarkdownTheme;

  private:
    TerminalOutput& _output;
    MarkdownTheme _theme;

    // Streaming state
    std::string _streamBuffer;
    bool _streaming = false;
    bool _inCodeBlock = false;
    bool _inThinkBlock = false;
    std::string _codeFence; ///< The fence string (e.g. "```") that opened the current code block.

    /// @brief Renders a single line of markdown (not inside a code block).
    void renderLine(std::string_view line);

    /// @brief Renders inline markdown elements (bold, italic, code, links).
    void renderInline(std::string_view text);

    /// @brief Processes buffered streaming content, rendering complete lines.
    void processStreamBuffer();

    /// @brief Renders a heading line.
    /// @param level The heading level (1-6).
    /// @param text The heading text.
    void renderHeading(int level, std::string_view text);
};

} // namespace tui
