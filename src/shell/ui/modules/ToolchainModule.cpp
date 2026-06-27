// SPDX-License-Identifier: Apache-2.0
#include "ToolchainModule.hpp"
#include <shell/ui/PromptColorResolver.hpp>

#include <tui/Theme.hpp>

#include <array>
#include <filesystem>
#include <string_view>

namespace endo
{

namespace
{

    struct ToolchainInfo
    {
        std::string icon;
        std::string name;
        bool valid = false;
    };

    [[nodiscard]] auto detectToolchain(std::string const& cwd) -> ToolchainInfo
    {
        auto const dir = std::filesystem::path(cwd);

        // Check common project files
        struct Detection
        {
            std::string_view file;
            std::string_view icon;
            std::string_view name;
        };

        // clang-format off
        static constexpr auto Detections = std::array<Detection, 8> {{
            { .file="Cargo.toml",      .icon="\xf0\x9f\xa6\x80", .name="rust" },      // U+1F980 crab
            { .file="package.json",    .icon="\xf0\x9f\x9f\xa9", .name="node" },      // U+1F7E9 green square
            { .file="go.mod",          .icon="\xf0\x9f\x90\xbf", .name="go" },        // U+1F43F chipmunk
            { .file="CMakeLists.txt",  .icon="\xe2\x9a\x99\xef\xb8\x8f", .name="cmake" }, // U+2699 gear
            { .file="pyproject.toml",  .icon="\xf0\x9f\x90\x8d", .name="python" },    // U+1F40D snake
            { .file="Gemfile",         .icon="\xf0\x9f\x92\x8e", .name="ruby" },      // U+1F48E gem
            { .file="pom.xml",         .icon="\xe2\x98\x95", .name="java" },           // U+2615 coffee
            { .file="build.zig",       .icon="\xe2\x9a\xa1", .name="zig" },            // U+26A1 lightning
        }};
        // clang-format on

        for (auto const& [file, icon, name]: Detections)
        {
            if (std::filesystem::exists(dir / file))
                return { .icon = std::string(icon), .name = std::string(name), .valid = true };
        }

        return {};
    }

} // namespace

bool ToolchainModule::shouldShow(PromptContext const& ctx) const
{
    return detectToolchain(ctx.cwd).valid;
}

PromptSegments ToolchainModule::evaluate(PromptContext const& ctx) const
{
    auto const info = detectToolchain(ctx.cwd);
    if (!info.valid)
        return {};

    auto style = tui::Style {};
    if (ctx.resolvedColors)
        style.fg = ctx.resolvedColors->badgeText.solid();
    else if (ctx.theme)
        style.fg = ctx.theme->promptColors.badgeText;

    return { PromptSegment { .text = info.icon + " " + info.name, .style = style } };
}

} // namespace endo
