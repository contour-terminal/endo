// SPDX-License-Identifier: Apache-2.0
#include "GitModule.hpp"

#include <tui/Theme.hpp>

#include <array>
#include <cstdio>
#include <string>

namespace endo
{

namespace
{

    struct GitInfo
    {
        std::string branch;
        int dirty = 0;  ///< Number of unstaged changes.
        int staged = 0; ///< Number of staged changes.
        bool valid = false;
    };

    /// @brief Runs a command and captures stdout.
    [[nodiscard]] auto runCommand(std::string const& cmd) -> std::string
    {
        auto result = std::string {};
        auto* fp = popen(cmd.c_str(), "r"); // NOLINT(cert-env33-c)
        if (!fp)
            return result;

        auto buf = std::array<char, 256> {};
        while (fgets(buf.data(), static_cast<int>(buf.size()), fp) != nullptr)
            result += buf.data();
        pclose(fp); // NOLINT(cert-env33-c)

        // Trim trailing newline
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
            result.pop_back();
        return result;
    }

    /// @brief Queries git status for the given directory.
    [[nodiscard]] auto queryGitInfo(std::string const& cwd) -> GitInfo
    {
        auto info = GitInfo {};

        // Get branch name
        info.branch = runCommand("git -C " + cwd + " rev-parse --abbrev-ref HEAD 2>/dev/null");
        if (info.branch.empty())
            return info;

        info.valid = true;

        // Get porcelain status for dirty/staged counts
        auto const status = runCommand("git -C " + cwd + " status --porcelain=v2 --branch 2>/dev/null");
        for (auto pos = std::size_t { 0 }; pos < status.size();)
        {
            auto const nl = status.find('\n', pos);
            auto const line = status.substr(pos, (nl == std::string::npos) ? std::string::npos : nl - pos);
            pos = (nl == std::string::npos) ? status.size() : nl + 1;

            if (line.size() >= 2 && (line[0] == '1' || line[0] == '2'))
            {
                // Changed entry: "1 XY ..." or "2 XY ..."
                auto const x = line[2]; // staged indicator
                auto const y = line[3]; // unstaged indicator
                if (x != '.')
                    ++info.staged;
                if (y != '.')
                    ++info.dirty;
            }
            else if (line.starts_with("? "))
            {
                ++info.dirty; // Untracked files count as dirty
            }
        }

        return info;
    }

} // namespace

bool GitModule::shouldShow(PromptContext const& ctx) const
{
    auto const branch = runCommand("git -C " + ctx.cwd + " rev-parse --abbrev-ref HEAD 2>/dev/null");
    return !branch.empty();
}

PromptSegments GitModule::evaluate(PromptContext const& ctx) const
{
    auto const info = queryGitInfo(ctx.cwd);
    if (!info.valid)
        return {};

    auto segments = PromptSegments {};

    // Branch icon
    auto branchStyle = tui::Style {};
    if (ctx.theme)
    {
        if (info.dirty > 0)
            branchStyle.fg = ctx.theme->promptColors.gitDirty;
        else if (info.staged > 0)
            branchStyle.fg = ctx.theme->promptColors.gitStaged;
        else
            branchStyle.fg = ctx.theme->promptColors.gitClean;
    }

    segments.push_back(
        PromptSegment { .text = "\xee\x82\xa0 " + info.branch, .style = branchStyle }); // U+E0A0

    // Dirty/staged indicators
    if (info.dirty > 0 || info.staged > 0)
    {
        auto indicatorText = std::string {};
        if (info.dirty > 0)
            indicatorText += " !" + std::to_string(info.dirty);
        if (info.staged > 0)
            indicatorText += " +" + std::to_string(info.staged);

        auto indicatorStyle = tui::Style {};
        if (ctx.theme)
            indicatorStyle.fg =
                (info.dirty > 0) ? ctx.theme->promptColors.gitDirty : ctx.theme->promptColors.gitStaged;
        segments.push_back(PromptSegment { .text = indicatorText, .style = indicatorStyle });
    }

    return segments;
}

} // namespace endo
