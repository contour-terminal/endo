// SPDX-License-Identifier: Apache-2.0
#include "GitModule.hpp"

#include <tui/Theme.hpp>

#include <array>
#include <cstdio>
#include <string>

#if defined(_WIN32)
    #define popen  _popen
    #define pclose _pclose
#endif

namespace endo
{

namespace
{

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

    /// @brief Queries git status using a single command.
    ///
    /// Extracts branch name from `# branch.head` header and dirty/staged counts
    /// from porcelain v2 change entries. Avoids the separate `git rev-parse` call.
    [[nodiscard]] auto queryGitInfo(std::string const& cwd) -> GitInfo
    {
        auto info = GitInfo {};

#if defined(_WIN32)
        auto const status = runCommand("git -C " + cwd + " status --porcelain=v2 --branch 2>NUL");
#else
        auto const status = runCommand("git -C " + cwd + " status --porcelain=v2 --branch 2>/dev/null");
#endif
        if (status.empty())
            return info;

        for (auto pos = std::size_t { 0 }; pos < status.size();)
        {
            auto const nl = status.find('\n', pos);
            auto const line = status.substr(pos, (nl == std::string::npos) ? std::string::npos : nl - pos);
            pos = (nl == std::string::npos) ? status.size() : nl + 1;

            // Extract branch name from header: "# branch.head <name>"
            if (line.starts_with("# branch.head "))
            {
                info.branch = line.substr(14); // strlen("# branch.head ") == 14
                // git status reports "(detached)" for detached HEAD — normalize to "HEAD"
                if (info.branch == "(detached)")
                    info.branch = "HEAD";
                info.valid = true;
                continue;
            }

            if (line.size() >= 4 && (line[0] == '1' || line[0] == '2'))
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

void GitModule::refreshIfNeeded(std::string const& cwd) const
{
    auto const now = std::chrono::steady_clock::now();

    if (_cachePopulated && _cachedCwd == cwd && (now - _cacheTime) < CacheTtl)
        return;

    _cache = queryGitInfo(cwd);
    _cachedCwd = cwd;
    _cacheTime = now;
    _cachePopulated = true;
}

bool GitModule::shouldShow(PromptContext const& ctx) const
{
    refreshIfNeeded(ctx.cwd);
    return _cache.valid;
}

PromptSegments GitModule::evaluate(PromptContext const& ctx) const
{
    refreshIfNeeded(ctx.cwd);

    if (!_cache.valid)
        return {};

    auto segments = PromptSegments {};

    // Branch icon
    auto branchStyle = tui::Style {};
    if (ctx.theme)
    {
        if (_cache.dirty > 0)
            branchStyle.fg = ctx.theme->promptColors.gitDirty;
        else if (_cache.staged > 0)
            branchStyle.fg = ctx.theme->promptColors.gitStaged;
        else
            branchStyle.fg = ctx.theme->promptColors.gitClean;
    }

    segments.push_back(
        PromptSegment { .text = "\xee\x82\xa0 " + _cache.branch, .style = branchStyle }); // U+E0A0

    // Dirty/staged indicators
    if (_cache.dirty > 0 || _cache.staged > 0)
    {
        auto indicatorText = std::string {};
        if (_cache.dirty > 0)
            indicatorText += " !" + std::to_string(_cache.dirty);
        if (_cache.staged > 0)
            indicatorText += " +" + std::to_string(_cache.staged);

        auto indicatorStyle = tui::Style {};
        if (ctx.theme)
            indicatorStyle.fg =
                (_cache.dirty > 0) ? ctx.theme->promptColors.gitDirty : ctx.theme->promptColors.gitStaged;
        segments.push_back(PromptSegment { .text = indicatorText, .style = indicatorStyle });
    }

    return segments;
}

} // namespace endo
