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
/// "status-files", "tracked-files", "config-keys".
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

    /// @brief Runs a command and returns stdout lines.
    [[nodiscard]] static std::vector<std::string> runCommand(std::string const& cmd);
};

} // namespace endo
