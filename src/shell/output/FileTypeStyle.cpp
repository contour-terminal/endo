// SPDX-License-Identifier: Apache-2.0
#include "FileTypeStyle.hpp"

#include <tui/SgrBuilder.hpp>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

using tui::RgbColor;
using tui::Style;

namespace endo
{

namespace
{
    /// Constructs an RgbColor from a hex value (e.g., rgb(0xFF6600)).
    [[nodiscard]] consteval RgbColor rgb(unsigned long long hex) noexcept
    {
        return { .r = static_cast<std::uint8_t>((hex >> 16) & 0xFF),
                 .g = static_cast<std::uint8_t>((hex >> 8) & 0xFF),
                 .b = static_cast<std::uint8_t>(hex & 0xFF) };
    }

    // ── Color palette ──────────────────────────────────────────────────────────

    // clang-format off
    constexpr auto DirColor        = rgb(0x5C7AFF); // bold blue
    constexpr auto ExecColor       = rgb(0x50FA7B); // bold green
    constexpr auto CppColor        = rgb(0x61AFEF); // blue
    constexpr auto PythonColor     = rgb(0x3776AB); // python blue
    constexpr auto RustColor       = rgb(0xDEA584); // rust orange
    constexpr auto JavaScriptColor = rgb(0xF7DF1E); // yellow
    constexpr auto TypeScriptColor = rgb(0x3178C6); // ts blue
    constexpr auto GoColor         = rgb(0x00ADD8); // go cyan
    constexpr auto JavaColor       = rgb(0xE76F00); // java orange
    constexpr auto RubyColor       = rgb(0xCC342D); // ruby red
    constexpr auto ShellColor      = rgb(0x4EAA25); // bash green
    constexpr auto MarkdownColor   = rgb(0xCCCCCC); // gray
    constexpr auto JsonColor       = rgb(0xF5C518); // yellow
    constexpr auto HtmlColor       = rgb(0xE34F26); // html orange
    constexpr auto CssColor        = rgb(0x1572B6); // css blue
    constexpr auto ArchiveColor    = rgb(0xE53935); // red
    constexpr auto ImageColor      = rgb(0xAB47BC); // magenta
    constexpr auto AudioColor      = rgb(0xAB47BC); // magenta
    constexpr auto VideoColor      = rgb(0xAB47BC); // magenta
    constexpr auto PdfColor        = rgb(0xE53935); // red
    constexpr auto TextColor       = rgb(0xCCCCCC); // gray
    constexpr auto LockColor       = rgb(0x999999); // dim gray
    constexpr auto GitColor        = rgb(0xF05032); // git orange-red
    constexpr auto DockerColor     = rgb(0x2496ED); // docker blue
    constexpr auto LuaColor        = rgb(0x000080); // lua blue
    constexpr auto PhpColor        = rgb(0x777BB4); // php purple
    constexpr auto SwiftColor      = rgb(0xF05138); // swift orange
    constexpr auto KotlinColor     = rgb(0x7F52FF); // kotlin purple
    constexpr auto HaskellColor    = rgb(0x5D4F85); // haskell purple
    constexpr auto ScalaColor      = rgb(0xDC322F); // scala red
    constexpr auto NixColor        = rgb(0x5277C3); // nix blue
    constexpr auto ZigColor        = rgb(0xF7A41D); // zig orange
    constexpr auto ElixirColor     = rgb(0x6E4A7E); // elixir purple
    constexpr auto CSharpColor     = rgb(0x239120); // c# green
    constexpr auto XmlColor        = rgb(0xE34F26); // xml orange
    constexpr auto SqlColor        = rgb(0x336791); // sql blue

    // ── Permission character colors (lsd-style) ─────────────────────────────────
    constexpr auto PermReadColor   = rgb(0x50C878); // green
    constexpr auto PermWriteColor  = rgb(0xE5C07B); // yellow
    constexpr auto PermExecColor   = rgb(0xE06C75); // red
    constexpr auto PermNoneColor   = rgb(0x5C6370); // dim gray
    // clang-format on

    // ── Nerd Font icons (UTF-8 encoded) ────────────────────────────────────────

    // clang-format off
    constexpr auto IconFolder     = "\xEF\x84\x95"; // U+F115 nf-fa-folder_open
    constexpr auto IconFile       = "\xEF\x80\x96"; // U+F016 nf-fa-file_o
    constexpr auto IconExec       = "\xEF\x80\x93"; // U+F013 nf-fa-cog
    constexpr auto IconTerminal   = "\xEF\x84\xA0"; // U+F120 nf-fa-terminal
    constexpr auto IconCpp        = "\xEE\x98\x9D"; // U+E61D nf-custom-c
    constexpr auto IconPython     = "\xEE\x9C\xBC"; // U+E73C nf-dev-python
    constexpr auto IconRust       = "\xEE\x9E\xA8"; // U+E7A8 nf-dev-rust
    constexpr auto IconJavaScript = "\xEE\x9D\x8E"; // U+E74E nf-dev-javascript
    constexpr auto IconTypeScript = "\xEE\x98\xA8"; // U+E628 nf-seti-typescript
    constexpr auto IconGo         = "\xEE\x98\xA7"; // U+E627 nf-seti-go
    constexpr auto IconJava       = "\xEE\x9C\xB8"; // U+E738 nf-dev-java
    constexpr auto IconRuby       = "\xEE\x9C\xB9"; // U+E739 nf-dev-ruby
    constexpr auto IconHtml       = "\xEE\x9C\xB6"; // U+E736 nf-dev-html5
    constexpr auto IconCss        = "\xEE\x9D\x89"; // U+E749 nf-dev-css3
    constexpr auto IconMarkdown   = "\xEE\x9C\xBE"; // U+E73E nf-dev-markdown
    constexpr auto IconJson       = "\xEE\x98\x8B"; // U+E60B nf-seti-json
    constexpr auto IconGit        = "\xEE\x9C\x82"; // U+E702 nf-dev-git
    constexpr auto IconDocker     = "\xEE\x9E\xB0"; // U+E7B0 nf-dev-docker
    constexpr auto IconLock       = "\xEF\x80\xA3"; // U+F023 nf-fa-lock
    constexpr auto IconImage      = "\xEF\x87\x85"; // U+F1C5 nf-fa-file_image_o
    constexpr auto IconAudio      = "\xEF\x87\x87"; // U+F1C7 nf-fa-file_audio_o
    constexpr auto IconVideo      = "\xEF\x87\x88"; // U+F1C8 nf-fa-file_video_o
    constexpr auto IconArchive    = "\xEF\x87\x86"; // U+F1C6 nf-fa-file_archive_o
    constexpr auto IconPdf        = "\xEF\x87\x81"; // U+F1C1 nf-fa-file_pdf_o
    constexpr auto IconText       = "\xEF\x83\xB6"; // U+F0F6 nf-fa-file_text_o
    constexpr auto IconDatabase   = "\xEF\x87\x80"; // U+F1C0 nf-fa-database
    constexpr auto IconLua        = "\xEE\x98\xA0"; // U+E620 nf-seti-lua
    constexpr auto IconPhp        = "\xEE\x9C\xBD"; // U+E73D nf-dev-php
    constexpr auto IconSwift      = "\xEE\x9D\x95"; // U+E755 nf-dev-swift
    constexpr auto IconKotlin     = "\xEE\x98\xB4"; // U+E634 nf-seti-kotlin
    constexpr auto IconHaskell    = "\xEE\x9D\xB7"; // U+E777 nf-dev-haskell
    constexpr auto IconScala      = "\xEE\x9C\xB7"; // U+E737 nf-dev-scala
    constexpr auto IconNix        = "\xEF\x8C\x93"; // U+F313 nf-linux-nixos
    constexpr auto IconZig        = "\xEE\x9A\xA9"; // U+E6A9 nf-seti-zig
    constexpr auto IconElixir     = "\xEE\x98\xAD"; // U+E62D nf-seti-elixir
    constexpr auto IconConfig     = "\xEE\x98\x95"; // U+E615 nf-seti-config
    // clang-format on

    // ── Extension-to-decoration table (sorted by extension for binary search) ──

    struct ExtensionMapping
    {
        std::string_view extension; ///< Lowercase extension without the dot.
        std::string_view icon;      ///< Nerd Font glyph (UTF-8).
        RgbColor color;             ///< Foreground color.
    };

    // clang-format off
    constexpr auto ExtensionTable = std::array<ExtensionMapping, 67> {{
        { .extension="avi",        .icon=IconVideo,      .color=VideoColor },
        { .extension="bash",       .icon=IconTerminal,   .color=ShellColor },
        { .extension="bz2",        .icon=IconArchive,    .color=ArchiveColor },
        { .extension="c",          .icon=IconCpp,        .color=CppColor },
        { .extension="cc",         .icon=IconCpp,        .color=CppColor },
        { .extension="cfg",        .icon=IconConfig,     .color=TextColor },
        { .extension="conf",       .icon=IconConfig,     .color=TextColor },
        { .extension="cpp",        .icon=IconCpp,        .color=CppColor },
        { .extension="cs",         .icon=IconCpp,        .color=CSharpColor },
        { .extension="css",        .icon=IconCss,        .color=CssColor },
        { .extension="csv",        .icon=IconText,       .color=TextColor },
        { .extension="cxx",        .icon=IconCpp,        .color=CppColor },
        { .extension="db",         .icon=IconDatabase,   .color=SqlColor },
        { .extension="dockerfile", .icon=IconDocker,     .color=DockerColor },
        { .extension="ex",         .icon=IconElixir,     .color=ElixirColor },
        { .extension="exs",        .icon=IconElixir,     .color=ElixirColor },
        { .extension="flac",       .icon=IconAudio,      .color=AudioColor },
        { .extension="gif",        .icon=IconImage,      .color=ImageColor },
        { .extension="gitignore",  .icon=IconGit,        .color=GitColor },
        { .extension="go",         .icon=IconGo,         .color=GoColor },
        { .extension="gz",         .icon=IconArchive,    .color=ArchiveColor },
        { .extension="h",          .icon=IconCpp,        .color=CppColor },
        { .extension="hpp",        .icon=IconCpp,        .color=CppColor },
        { .extension="hs",         .icon=IconHaskell,    .color=HaskellColor },
        { .extension="htm",        .icon=IconHtml,       .color=HtmlColor },
        { .extension="html",       .icon=IconHtml,       .color=HtmlColor },
        { .extension="java",       .icon=IconJava,       .color=JavaColor },
        { .extension="jpeg",       .icon=IconImage,      .color=ImageColor },
        { .extension="jpg",        .icon=IconImage,      .color=ImageColor },
        { .extension="js",         .icon=IconJavaScript, .color=JavaScriptColor },
        { .extension="json",       .icon=IconJson,       .color=JsonColor },
        { .extension="jsx",        .icon=IconJavaScript, .color=JavaScriptColor },
        { .extension="kt",         .icon=IconKotlin,     .color=KotlinColor },
        { .extension="lock",       .icon=IconLock,       .color=LockColor },
        { .extension="log",        .icon=IconText,       .color=TextColor },
        { .extension="lua",        .icon=IconLua,        .color=LuaColor },
        { .extension="md",         .icon=IconMarkdown,   .color=MarkdownColor },
        { .extension="mkv",        .icon=IconVideo,      .color=VideoColor },
        { .extension="mp3",        .icon=IconAudio,      .color=AudioColor },
        { .extension="mp4",        .icon=IconVideo,      .color=VideoColor },
        { .extension="nix",        .icon=IconNix,        .color=NixColor },
        { .extension="pdf",        .icon=IconPdf,        .color=PdfColor },
        { .extension="php",        .icon=IconPhp,        .color=PhpColor },
        { .extension="png",        .icon=IconImage,      .color=ImageColor },
        { .extension="py",         .icon=IconPython,     .color=PythonColor },
        { .extension="rb",         .icon=IconRuby,       .color=RubyColor },
        { .extension="rs",         .icon=IconRust,       .color=RustColor },
        { .extension="scala",      .icon=IconScala,      .color=ScalaColor },
        { .extension="scss",       .icon=IconCss,        .color=CssColor },
        { .extension="sh",         .icon=IconTerminal,   .color=ShellColor },
        { .extension="sql",        .icon=IconDatabase,   .color=SqlColor },
        { .extension="svg",        .icon=IconImage,      .color=ImageColor },
        { .extension="swift",      .icon=IconSwift,      .color=SwiftColor },
        { .extension="tar",        .icon=IconArchive,    .color=ArchiveColor },
        { .extension="toml",       .icon=IconJson,       .color=JsonColor },
        { .extension="ts",         .icon=IconTypeScript, .color=TypeScriptColor },
        { .extension="tsx",        .icon=IconTypeScript, .color=TypeScriptColor },
        { .extension="txt",        .icon=IconText,       .color=TextColor },
        { .extension="wav",        .icon=IconAudio,      .color=AudioColor },
        { .extension="webp",       .icon=IconImage,      .color=ImageColor },
        { .extension="xml",        .icon=IconHtml,       .color=XmlColor },
        { .extension="xz",         .icon=IconArchive,    .color=ArchiveColor },
        { .extension="yaml",       .icon=IconJson,       .color=JsonColor },
        { .extension="yml",        .icon=IconJson,       .color=JsonColor },
        { .extension="zig",        .icon=IconZig,        .color=ZigColor },
        { .extension="zip",        .icon=IconArchive,    .color=ArchiveColor },
        { .extension="zsh",        .icon=IconTerminal,   .color=ShellColor },
    }};
    // clang-format on

    // Verify that the table is sorted at compile time.
    static_assert([]() constexpr {
        for (size_t i = 1; i < ExtensionTable.size(); ++i)
            if (ExtensionTable[i - 1].extension >= ExtensionTable[i].extension)
                return false;
        return true;
    }());

    /// Extracts the file extension (lowercase, without dot) from a filename.
    [[nodiscard]] std::string extractExtension(std::string_view name)
    {
        // Handle special filenames like "Dockerfile" and ".gitignore"
        if (name == "Dockerfile" || name == "dockerfile")
            return "dockerfile";
        if (name == ".gitignore")
            return "gitignore";

        auto const dotPos = name.rfind('.');
        if (dotPos == std::string_view::npos || dotPos == 0 || dotPos == name.size() - 1)
            return {};

        auto ext = std::string(name.substr(dotPos + 1));
        std::ranges::transform(ext, ext.begin(), [](unsigned char c) { return std::tolower(c); });
        return ext;
    }
} // namespace

FileDecoration getFileDecoration(std::string_view name, bool isDir, int64_t mode)
{
    auto decoration = FileDecoration {};

    // 1. Directory
    if (isDir)
    {
        decoration.icon = IconFolder;
        decoration.style.fg = DirColor;
        decoration.style.bold = true;
    }
    // 2. Executable (non-directory with any execute bit set)
    else if ((mode & 0111) != 0)
    {
        decoration.icon = IconExec;
        decoration.style.fg = ExecColor;
        decoration.style.bold = true;

        // Still try extension match for better icon
        auto const ext = extractExtension(name);
        if (!ext.empty())
        {
            auto const it = // NOLINT(readability-qualified-auto)
                std::ranges::lower_bound(ExtensionTable, ext, {}, &ExtensionMapping::extension);
            if (it != ExtensionTable.end() && it->extension == ext)
                decoration.icon = it->icon;
        }
    }
    // 3. Extension match
    else
    {
        auto const ext = extractExtension(name);
        if (!ext.empty())
        {
            auto const it = // NOLINT(readability-qualified-auto)
                std::ranges::lower_bound(ExtensionTable, ext, {}, &ExtensionMapping::extension);
            if (it != ExtensionTable.end() && it->extension == ext)
            {
                decoration.icon = it->icon;
                decoration.style.fg = it->color;
            }
            else
            {
                decoration.icon = IconFile;
            }
        }
        else
        {
            // 4. Fallback
            decoration.icon = IconFile;
        }
    }

    // Hidden files get the dim attribute (additive)
    if (!name.empty() && name[0] == '.')
        decoration.style.dim = true;

    return decoration;
}

std::string sgrSequence(tui::Style const& style)
{
    return tui::buildSgrSequence(style);
}

std::string colorizePermissions(std::string_view perms)
{
    // Build SGR sequences for each permission character type once.
    static auto const sgrRead = sgrSequence({ .fg = PermReadColor });
    static auto const sgrWrite = sgrSequence({ .fg = PermWriteColor });
    static auto const sgrExec = sgrSequence({ .fg = PermExecColor });
    static auto const sgrNone = sgrSequence({ .fg = PermNoneColor });
    static constexpr auto SgrReset = "\033[m";

    std::string result;
    result.reserve(perms.size() * 20); // pre-allocate for SGR overhead
    for (auto const c: perms)
    {
        switch (c)
        {
            case 'r': result += sgrRead; break;
            case 'w': result += sgrWrite; break;
            case 'x': result += sgrExec; break;
            case '-': result += sgrNone; break;
            default: break;
        }
        result += c;
        result += SgrReset;
    }
    return result;
}

} // namespace endo
