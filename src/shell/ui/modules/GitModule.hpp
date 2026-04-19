// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/ui/PromptModule.hpp>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace endo
{

/// @brief Cached git repository information.
struct GitInfo
{
    std::string branch;
    int dirty = 0;  ///< Number of unstaged changes.
    int staged = 0; ///< Number of staged changes.
    bool valid = false;
};

/// @brief Prompt module that displays Git branch and status information.
///
/// Shows the current branch name with color-coded dirty/clean/staged states.
/// The `git status --porcelain=v2 --branch` subprocess runs on a background
/// thread so the prompt never blocks the main event loop; the prompt is
/// redrawn once the result arrives.
class GitModule final: public PromptModule
{
  public:
    /// @brief Function that fetches git info for a given cwd.
    using QueryFn = std::function<GitInfo(std::string const&)>;

    /// @brief Constructs the module.
    /// @param queryFn Query implementation. Defaults to the `popen`-based
    ///                `git status` call. Tests inject a mock to control timing.
    explicit GitModule(QueryFn queryFn = {});

    [[nodiscard]] std::string_view id() const noexcept override { return "git"; }

    [[nodiscard]] ModuleSensitivity sensitivity() const override { return ModuleSensitivity::CwdChange; }

    [[nodiscard]] PromptSegments evaluate(PromptContext const& ctx) const override;
    [[nodiscard]] bool shouldShow(PromptContext const& ctx) const override;

    /// @brief Returns a short refresh interval while a background fetch is in
    ///        flight (so the prompt ticks until the result arrives) and a long
    ///        idle interval otherwise.
    [[nodiscard]] std::optional<std::chrono::milliseconds> refreshInterval() const override;

    void invalidateCache() override { _cachePopulated = false; }

    /// @brief Returns the cached git info from the most recent completed query.
    [[nodiscard]] GitInfo const& cachedInfo() const noexcept { return _cache; }

  private:
    /// @brief Shared slot for communication with a detached worker thread.
    ///
    /// The worker writes `result` and then stores `ready = true` with release
    /// ordering; the main thread observes `ready` with acquire ordering and
    /// then reads `result`. `cwd` is written only on launch (main thread,
    /// before the worker starts) and is never mutated afterwards.
    struct PendingFetch
    {
        std::string cwd;
        std::atomic<bool> ready { false };
        GitInfo result;
    };

    /// @brief TTL for git info cache.
    static constexpr auto _cacheTtl = std::chrono::seconds(3); // NOLINT(readability-identifier-naming)

    /// @brief Refresh interval while no background fetch is in flight.
    static constexpr auto _idleInterval = // NOLINT(readability-identifier-naming)
        std::chrono::seconds(5);

    /// @brief Refresh interval while a background fetch is in flight.
    static constexpr auto _pendingInterval = // NOLINT(readability-identifier-naming)
        std::chrono::milliseconds(150);

    /// @brief Consumes a completed fetch into `_cache` (if the cwd still
    ///        matches) and clears `_pending`. No-op if no fetch is pending or
    ///        the worker has not finished yet.
    void consumePendingIfReady(std::string const& cwd) const;

    /// @brief Launches a detached worker thread to fetch git info for `cwd`.
    void launchFetch(std::string const& cwd) const;

    /// @brief Ensures a fresh git info result is either cached or being fetched
    ///        for `cwd`. Never blocks.
    void refreshIfNeeded(std::string const& cwd) const;

    QueryFn _queryFn;
    mutable GitInfo _cache;
    mutable std::string _cachedCwd;
    mutable std::chrono::steady_clock::time_point _cacheTime;
    mutable bool _cachePopulated = false;
    mutable std::shared_ptr<PendingFetch> _pending;
};

} // namespace endo
