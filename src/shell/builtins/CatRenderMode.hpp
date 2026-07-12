// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tui/GenericSyntaxHighlighter.hpp>

#include <cstdint>

namespace endo
{

/// @brief Default left margin, in columns, for markdown rendered by `cat`.
///
/// A single column separates the document from the terminal's left edge, which
/// makes the first word of each line easier to read. Override with `--indent`.
constexpr auto DefaultMarkdownIndent = 1;

/// @brief How the `cat` builtin should present one file's contents.
enum class CatRenderMode : std::uint8_t
{
    Raw,        ///< Emit the file's bytes verbatim; no rendering, no line processing.
    Text,       ///< Line pipeline (numbering, squeeze, ranges), syntax-highlighted on a TTY.
    Markdown,   ///< Render markdown: tables, OSC-8 links, big titles, inline Sixel images.
    SixelImage, ///< Decode the file as an image and emit it as Sixel.
};

/// @brief The inputs that decide how `cat` presents a file.
///
/// Grouping them keeps the decision a pure function that can be exhaustively
/// unit-tested, rather than a set of conditions buried in the builtin.
struct CatRenderContext
{
    bool rawMode = false;                             ///< `--raw` was given.
    bool outputIsTty = false;                         ///< The output handle refers to a terminal.
    bool hasProcessingFlags = false;                  ///< Any of -n, -b, -s, -E, -T, -r was given.
    bool isImageExt = false;                          ///< The path carries a supported image extension.
    bool forceImage = false;                          ///< `-c/--columns` or `-R/--rows` was given.
    tui::LanguageId language = tui::LanguageId::None; ///< Language detected from the path.
};

/// @brief Decides how `cat` should present a file.
///
/// Precedence, highest first:
/// 1. `--raw` suppresses all rendering. Line-processing flags still apply, since
///    they describe a view of the source text rather than a rendering of it.
/// 2. Image files render as Sixel, as they already did before markdown support.
/// 3. Markdown renders only on a terminal, and only when no line-processing flag
///    asked for a literal view of the source.
/// 4. Everything else goes through the text pipeline.
///
/// @param context The inputs describing the file and the output handle.
/// @return The presentation mode.
[[nodiscard]] constexpr auto chooseCatRenderMode(CatRenderContext const& context) noexcept -> CatRenderMode
{
    if (context.rawMode)
        return context.hasProcessingFlags ? CatRenderMode::Text : CatRenderMode::Raw;

    if (context.isImageExt && (context.outputIsTty || context.forceImage))
        return CatRenderMode::SixelImage;

    if (context.language == tui::LanguageId::Markdown && context.outputIsTty && !context.hasProcessingFlags)
        return CatRenderMode::Markdown;

    return CatRenderMode::Text;
}

/// @brief Whether syntax highlighting should be applied in CatRenderMode::Text.
///
/// Highlighting is a rendering, so `--raw` disables it, and it is meaningless
/// when the output is not a terminal.
///
/// @param outputIsTty Whether the output handle refers to a terminal.
/// @param rawMode Whether `--raw` was given.
/// @param language The language detected from the path.
/// @return true when highlighted output should be produced.
[[nodiscard]] constexpr auto shouldHighlightCatOutput(bool outputIsTty,
                                                      bool rawMode,
                                                      tui::LanguageId language) noexcept -> bool
{
    return outputIsTty && !rawMode && language != tui::LanguageId::None;
}

} // namespace endo
