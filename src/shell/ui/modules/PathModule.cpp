// SPDX-License-Identifier: Apache-2.0
#include "PathModule.hpp"
#include <shell/ui/PromptColorResolver.hpp>

#include <platform/FileUri.hpp>
#include <platform/PathUtils.hpp>

#include <tui/Theme.hpp>

#include <algorithm>
#include <cctype>
#include <string_view>

namespace endo
{

namespace
{
    /// @brief Tests whether @p path begins with @p prefix.
    ///
    /// On Windows the comparison is case-insensitive, matching the case-insensitive
    /// filesystem (the home directory and cwd may differ in case despite being equal).
    /// On POSIX it is an exact prefix test.
    ///
    /// @param path   The full path to test.
    /// @param prefix The candidate prefix.
    /// @return True if @p path starts with @p prefix.
    [[nodiscard]] bool startsWithPath(std::string_view path, std::string_view prefix)
    {
        if (prefix.size() > path.size())
            return false;
#if defined(_WIN32)
        return std::ranges::equal(path.substr(0, prefix.size()), prefix, [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
        });
#else
        return path.starts_with(prefix);
#endif
    }
} // namespace

PromptSegments PathModule::evaluate(PromptContext const& ctx) const
{
    auto path = ctx.cwd;

    // Tilde-contract home directory
    if (!ctx.homePath.empty() && startsWithPath(path, ctx.homePath))
    {
        path = "~" + path.substr(ctx.homePath.size());
        if (path.size() > 1 && path[1] != '/')
            path = ctx.cwd; // Restore if not a clean prefix match
    }

    auto style = tui::Style {};
    if (ctx.resolvedColors)
        style.fg = ctx.resolvedColors->path.solid();
    else if (ctx.theme)
        style.fg = ctx.theme->promptColors.path;
    style.bold = true;

    // Link the real cwd, not the tilde-contracted text the user sees.
    auto hyperlink =
        ctx.hyperlinks ? platform::fileUri(platform::normalizePath(ctx.cwd), ctx.hostname) : std::string {};

    return { PromptSegment { .text = std::move(path), .style = style, .hyperlink = std::move(hyperlink) } };
}

} // namespace endo
