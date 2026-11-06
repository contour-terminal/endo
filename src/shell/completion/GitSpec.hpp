// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/completion/CommandQueryProvider.hpp>
#include <shell/completion/CommandSpec.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace endo
{

/// @brief Creates the full git CommandSpec with subcommand definitions.
///
/// Includes Tier 1 subcommands (full definitions with options and args),
/// Tier 2 (key options), and Tier 3 (name + description only).
[[nodiscard]] CommandSpec createGitSpec();

/// @brief Git-specific query provider for dynamic data (branches, tags, etc.).
///
/// Resolves queryTags: "branches", "local-branches", "remote-branches",
/// "tags", "remotes", "stashes", "recent-commits", "aliases",
/// "status-files", "tracked-files", "config-keys", "worktrees".
class GitQueryProvider: public CommandQueryProvider
{
  public:
    [[nodiscard]] std::vector<QueryResult> query(std::string_view queryTag) override;

  private:
    [[nodiscard]] static std::vector<QueryResult> queryBranches(bool localOnly, bool remoteOnly);
    [[nodiscard]] static std::vector<QueryResult> queryTags();
    [[nodiscard]] static std::vector<QueryResult> queryRemotes();
    [[nodiscard]] static std::vector<QueryResult> queryStashes();
    [[nodiscard]] static std::vector<QueryResult> queryRecentCommits();
    [[nodiscard]] static std::vector<QueryResult> queryAliases();
    [[nodiscard]] static std::vector<QueryResult> queryStatusFiles();
    [[nodiscard]] static std::vector<QueryResult> queryTrackedFiles();
    [[nodiscard]] static std::vector<QueryResult> queryConfigKeys();
    [[nodiscard]] static std::vector<QueryResult> queryWorktrees();

    /// @brief Runs git with @p arguments, directly and with no shell, and returns its non-empty stdout lines.
    /// @param arguments The arguments after `git`.
    /// @return The lines git wrote, without line endings; empty when git could not be run.
    [[nodiscard]] static std::vector<std::string> runGit(std::vector<std::string> const& arguments);
};

} // namespace endo
