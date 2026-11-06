// SPDX-License-Identifier: Apache-2.0
#include "GitModule.hpp"
#include <shell/ui/PromptColorResolver.hpp>

#include <core/Environment.hpp>
#include <core/tui/Theme.hpp>

#include <array>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <platform/CaptureOutput.hpp>

namespace endo
{

namespace
{

    /// @brief Default query implementation: runs `git status` directly, with no shell between.
    ///
    /// Through @c platform::captureOutput, so the directory is an argument rather than text a shell
    /// parses -- a space, `&` or `$(...)` in its name is only a name -- `git` is the one on PATH rather
    /// than one in that directory, and on Windows the child does not attach to this shell's console,
    /// which a child cannot start on once the console's host has gone.
    [[nodiscard]] auto defaultQueryGitInfo(std::string const& cwd) -> GitInfo
    {
        auto const arguments =
            std::array<std::string, 5> { "-C", cwd, "status", "--porcelain=v2", "--branch" };
        // The output alone decides: git writes the porcelain only where it succeeds, and the exit
        // status may be missing, reaped by the shell's own SIGCHLD handler first.
        return platform::captureOutput("git", arguments, core::LiveEnvironment {})
            .transform([](platform::CapturedOutput const& captured) {
                return parseGitStatus(captured.standardOutput);
            })
            .value_or(GitInfo {});
    }

} // namespace

GitInfo parseGitStatus(std::string_view porcelainV2)
{
    auto info = GitInfo {};

    for (auto const lineRange: std::views::split(porcelainV2, '\n'))
    {
        auto const line = std::string_view(lineRange.begin(), lineRange.end());

        // Extract branch name from header: "# branch.head <name>"
        if (line.starts_with("# branch.head "))
        {
            info.branch = std::string(line.substr(14)); // strlen("# branch.head ") == 14
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

GitModule::GitModule(QueryFn queryFn):
    _queryFn(queryFn ? std::move(queryFn) : QueryFn { &defaultQueryGitInfo })
{
}

void GitModule::consumePendingIfReady(std::string const& cwd) const
{
    if (!_pending)
        return;
    if (!_pending->ready.load(std::memory_order_acquire))
        return;

    // Acquire above pairs with the worker's release-store after writing result;
    // the read below sees the fully-constructed GitInfo.
    if (_pending->cwd == cwd)
    {
        _cache = std::move(_pending->result);
        _cachedCwd = _pending->cwd;
        _cacheTime = std::chrono::steady_clock::now();
        _cachePopulated = true;
    }
    _pending.reset();
}

void GitModule::launchFetch(std::string const& cwd) const
{
    auto slot = std::make_shared<PendingFetch>();
    slot->cwd = cwd;
    _pending = slot;

    // The worker owns its own shared_ptr to `slot`, so the slot outlives the
    // GitModule if the main thread clears `_pending` (e.g., cwd changed) or
    // the module is destroyed while the worker is still running.
    std::thread([slot, cwd, query = _queryFn]() mutable {
        slot->result = query(cwd);
        slot->ready.store(true, std::memory_order_release);
    }).detach();
}

void GitModule::refreshIfNeeded(std::string const& cwd) const
{
    consumePendingIfReady(cwd);

    auto const now = std::chrono::steady_clock::now();
    if (_cachePopulated && _cachedCwd == cwd && (now - _cacheTime) < _cacheTtl)
        return;

    // An in-flight fetch for a different cwd is stale: drop our handle. The
    // worker still holds a shared_ptr to the slot and runs to completion, but
    // its result is never consumed because the shared slot is no longer
    // referenced by us.
    if (_pending && _pending->cwd != cwd)
        _pending.reset();

    if (!_pending)
        launchFetch(cwd);
}

std::optional<std::chrono::milliseconds> GitModule::refreshInterval() const
{
    if (_pending)
        return _pendingInterval;
    return _idleInterval;
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
    auto branchStyle = core::tui::Style {};
    if (ctx.resolvedColors)
    {
        if (_cache.dirty > 0)
            branchStyle.fg = ctx.resolvedColors->gitDirty.solid();
        else if (_cache.staged > 0)
            branchStyle.fg = ctx.resolvedColors->gitStaged.solid();
        else
            branchStyle.fg = ctx.resolvedColors->gitClean.solid();
    }
    else if (ctx.theme)
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

        auto indicatorStyle = core::tui::Style {};
        if (ctx.resolvedColors)
            indicatorStyle.fg = (_cache.dirty > 0) ? ctx.resolvedColors->gitDirty.solid()
                                                   : ctx.resolvedColors->gitStaged.solid();
        else if (ctx.theme)
            indicatorStyle.fg =
                (_cache.dirty > 0) ? ctx.theme->promptColors.gitDirty : ctx.theme->promptColors.gitStaged;
        segments.push_back(PromptSegment { .text = indicatorText, .style = indicatorStyle });
    }

    return segments;
}

} // namespace endo
