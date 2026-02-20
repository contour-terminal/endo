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
    [[nodiscard]] std::vector<QueryResult> queryBranches(bool localOnly, bool remoteOnly) const;
    [[nodiscard]] std::vector<QueryResult> queryTags() const;
    [[nodiscard]] std::vector<QueryResult> queryRemotes() const;
    [[nodiscard]] std::vector<QueryResult> queryStashes() const;
    [[nodiscard]] std::vector<QueryResult> queryRecentCommits() const;
    [[nodiscard]] std::vector<QueryResult> queryAliases() const;
    [[nodiscard]] std::vector<QueryResult> queryStatusFiles() const;
    [[nodiscard]] std::vector<QueryResult> queryTrackedFiles() const;
    [[nodiscard]] std::vector<QueryResult> queryConfigKeys() const;

    /// @brief Runs a command and returns stdout lines.
    [[nodiscard]] static std::vector<std::string> runCommand(std::string const& cmd);
};

} // namespace endo
