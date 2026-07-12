// SPDX-License-Identifier: Apache-2.0
#include <shell/builtins/CatRenderMode.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace endo;
using tui::LanguageId;

namespace
{
/// @brief A context describing a markdown file shown on an interactive terminal.
constexpr auto markdownOnTty() -> CatRenderContext
{
    return CatRenderContext { .outputIsTty = true, .language = LanguageId::Markdown };
}
} // namespace

// ============================================================================
// Markdown selection
// ============================================================================

TEST_CASE("chooseCatRenderMode.markdown_on_tty", "[cat]")
{
    STATIC_CHECK(chooseCatRenderMode(markdownOnTty()) == CatRenderMode::Markdown);
}

TEST_CASE("chooseCatRenderMode.markdown_off_tty_is_text", "[cat]")
{
    auto context = markdownOnTty();
    context.outputIsTty = false;
    CHECK(chooseCatRenderMode(context) == CatRenderMode::Text);
}

TEST_CASE("chooseCatRenderMode.markdown_with_raw_is_raw", "[cat]")
{
    auto context = markdownOnTty();
    context.rawMode = true;
    CHECK(chooseCatRenderMode(context) == CatRenderMode::Raw);
}

TEST_CASE("chooseCatRenderMode.markdown_with_processing_flags_is_text", "[cat]")
{
    auto context = markdownOnTty();
    context.hasProcessingFlags = true;
    CHECK(chooseCatRenderMode(context) == CatRenderMode::Text);
}

TEST_CASE("chooseCatRenderMode.image_sizing_flags_do_not_block_markdown", "[cat]")
{
    // -c/-R describe image geometry and say nothing about the source text.
    auto context = markdownOnTty();
    context.forceImage = true;
    CHECK(chooseCatRenderMode(context) == CatRenderMode::Markdown);
}

// ============================================================================
// --raw precedence
// ============================================================================

TEST_CASE("chooseCatRenderMode.raw_beats_image", "[cat]")
{
    auto const context =
        CatRenderContext { .rawMode = true, .outputIsTty = true, .isImageExt = true, .forceImage = true };
    CHECK(chooseCatRenderMode(context) == CatRenderMode::Raw);
}

TEST_CASE("chooseCatRenderMode.raw_with_processing_flags_keeps_the_text_pipeline", "[cat]")
{
    // --raw suppresses rendering, not line numbering: `cat --raw -n f` still numbers.
    auto const context =
        CatRenderContext { .rawMode = true, .outputIsTty = true, .hasProcessingFlags = true };
    CHECK(chooseCatRenderMode(context) == CatRenderMode::Text);
}

TEST_CASE("chooseCatRenderMode.raw_without_flags_is_a_byte_dump", "[cat]")
{
    auto const context = CatRenderContext { .rawMode = true, .outputIsTty = true };
    CHECK(chooseCatRenderMode(context) == CatRenderMode::Raw);
}

// ============================================================================
// Image files (behavior predating markdown support must be preserved)
// ============================================================================

TEST_CASE("chooseCatRenderMode.image_on_tty", "[cat]")
{
    auto const context = CatRenderContext { .outputIsTty = true, .isImageExt = true };
    CHECK(chooseCatRenderMode(context) == CatRenderMode::SixelImage);
}

TEST_CASE("chooseCatRenderMode.image_off_tty_is_text", "[cat]")
{
    auto const context = CatRenderContext { .outputIsTty = false, .isImageExt = true };
    CHECK(chooseCatRenderMode(context) == CatRenderMode::Text);
}

TEST_CASE("chooseCatRenderMode.image_off_tty_with_force_is_sixel", "[cat]")
{
    // -c/-R force image rendering even into a pipe, as they did before.
    auto const context = CatRenderContext { .outputIsTty = false, .isImageExt = true, .forceImage = true };
    CHECK(chooseCatRenderMode(context) == CatRenderMode::SixelImage);
}

TEST_CASE("chooseCatRenderMode.image_beats_markdown_when_both_somehow_apply", "[cat]")
{
    auto const context =
        CatRenderContext { .outputIsTty = true, .isImageExt = true, .language = LanguageId::Markdown };
    CHECK(chooseCatRenderMode(context) == CatRenderMode::SixelImage);
}

// ============================================================================
// Other languages
// ============================================================================

TEST_CASE("chooseCatRenderMode.source_file_on_tty_is_text", "[cat]")
{
    auto const context = CatRenderContext { .outputIsTty = true, .language = LanguageId::Cpp };
    CHECK(chooseCatRenderMode(context) == CatRenderMode::Text);
}

TEST_CASE("chooseCatRenderMode.unknown_language_is_text", "[cat]")
{
    auto const context = CatRenderContext { .outputIsTty = true, .language = LanguageId::None };
    CHECK(chooseCatRenderMode(context) == CatRenderMode::Text);
}

// ============================================================================
// Exhaustive: rendering never happens off a terminal, and --raw always wins
// ============================================================================

TEST_CASE("chooseCatRenderMode.no_rendering_without_a_terminal", "[cat]")
{
    for (auto const rawMode: { false, true })
        for (auto const hasFlags: { false, true })
            for (auto const isImage: { false, true })
                for (auto const language: { LanguageId::None, LanguageId::Cpp, LanguageId::Markdown })
                {
                    auto const context = CatRenderContext { .rawMode = rawMode,
                                                            .outputIsTty = false,
                                                            .hasProcessingFlags = hasFlags,
                                                            .isImageExt = isImage,
                                                            .forceImage = false,
                                                            .language = language };
                    auto const mode = chooseCatRenderMode(context);
                    INFO("raw=" << rawMode << " flags=" << hasFlags << " image=" << isImage);
                    CHECK(mode != CatRenderMode::Markdown);
                    CHECK(mode != CatRenderMode::SixelImage);
                }
}

TEST_CASE("chooseCatRenderMode.raw_never_renders", "[cat]")
{
    for (auto const isTty: { false, true })
        for (auto const hasFlags: { false, true })
            for (auto const isImage: { false, true })
                for (auto const forceImage: { false, true })
                    for (auto const language: { LanguageId::None, LanguageId::Markdown })
                    {
                        auto const context = CatRenderContext { .rawMode = true,
                                                                .outputIsTty = isTty,
                                                                .hasProcessingFlags = hasFlags,
                                                                .isImageExt = isImage,
                                                                .forceImage = forceImage,
                                                                .language = language };
                        auto const mode = chooseCatRenderMode(context);
                        CHECK((mode == CatRenderMode::Raw || mode == CatRenderMode::Text));
                    }
}

// ============================================================================
// Highlighting
// ============================================================================

TEST_CASE("shouldHighlightCatOutput.requires_tty_and_a_language", "[cat]")
{
    STATIC_CHECK(shouldHighlightCatOutput(true, false, LanguageId::Cpp));
    STATIC_CHECK_FALSE(shouldHighlightCatOutput(false, false, LanguageId::Cpp));
    STATIC_CHECK_FALSE(shouldHighlightCatOutput(true, false, LanguageId::None));
}

TEST_CASE("shouldHighlightCatOutput.raw_disables_highlighting", "[cat]")
{
    STATIC_CHECK_FALSE(shouldHighlightCatOutput(true, true, LanguageId::Cpp));
}
