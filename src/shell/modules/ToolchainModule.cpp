// SPDX-License-Identifier: Apache-2.0
#include "ToolchainModule.hpp"

#include <tui/Theme.hpp>

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
        static constexpr auto detections = std::array<Detection, 8> {{
            { "Cargo.toml",      "\xf0\x9f\xa6\x80", "rust" },      // U+1F980 crab
            { "package.json",    "\xf0\x9f\x9f\xa9", "node" },      // U+1F7E9 green square
            { "go.mod",          "\xf0\x9f\x90\xbf", "go" },        // U+1F43F chipmunk
            { "CMakeLists.txt",  "\xe2\x9a\x99\xef\xb8\x8f", "cmake" }, // U+2699 gear
            { "pyproject.toml",  "\xf0\x9f\x90\x8d", "python" },    // U+1F40D snake
            { "Gemfile",         "\xf0\x9f\x92\x8e", "ruby" },      // U+1F48E gem
            { "pom.xml",         "\xe2\x98\x95", "java" },           // U+2615 coffee
            { "build.zig",       "\xe2\x9a\xa1", "zig" },            // U+26A1 lightning
        }};
        // clang-format on

        for (auto const& [file, icon, name]: detections)
        {
            if (std::filesystem::exists(dir / file))
                return { std::string(icon), std::string(name), true };
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
    if (ctx.theme)
        style.fg = ctx.theme->promptColors.badgeText;

    return { PromptSegment { .text = info.icon + " " + info.name, .style = style } };
}

} // namespace endo
