// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/ui/PromptModule.hpp>

#include <chrono>
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
/// Uses a single `git status --porcelain=v2 --branch` call with TTL-based caching
/// to minimize subprocess overhead.
class GitModule final: public PromptModule
{
  public:
    [[nodiscard]] std::string_view id() const noexcept override { return "git"; }

    [[nodiscard]] PromptSegments evaluate(PromptContext const& ctx) const override;
    [[nodiscard]] bool shouldShow(PromptContext const& ctx) const override;
    void invalidateCache() override { _cachePopulated = false; }

    /// @brief Returns the cached git info from the most recent query.
    [[nodiscard]] GitInfo const& cachedInfo() const noexcept { return _cache; }

  private:
    /// @brief TTL for git info cache.
    static constexpr auto CacheTtl = std::chrono::seconds(3);

    /// @brief Refreshes the cache if the CWD changed or the TTL expired.
    /// @param cwd The current working directory.
    void refreshIfNeeded(std::string const& cwd) const;

    mutable GitInfo _cache;
    mutable std::string _cachedCwd;
    mutable std::chrono::steady_clock::time_point _cacheTime;
    mutable bool _cachePopulated = false;
};

} // namespace endo
