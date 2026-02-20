// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/completion/CommandQueryProvider.hpp>
#include <shell/completion/CommandSpec.hpp>

#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace endo
{

/// @brief Creates the ssh CommandSpec with common options and host positional arg.
///
/// Models ssh's most frequently used options as global flags/values, and the
/// destination `[user@]hostname` as a DynamicQuery positional argument resolved
/// via SshQueryProvider at completion time.
[[nodiscard]] CommandSpec createSshSpec();

/// @brief Creates the scp CommandSpec with common options and host positional arg.
///
/// Shares the same "hosts" query tag as ssh for hostname completion.
[[nodiscard]] CommandSpec createScpSpec();

/// @brief Query provider for ssh/scp host names from ~/.ssh/config.
///
/// Parses `~/.ssh/config` (and recursively follows `Include` directives),
/// extracting `Host` aliases and optional `HostName` real addresses.
/// Wildcard-only entries (`*`, patterns containing `*` or `?`) are skipped.
/// Resolves queryTag: "hosts".
class SshQueryProvider: public CommandQueryProvider
{
  public:
    [[nodiscard]] std::vector<QueryResult> query(std::string_view queryTag) override;

    /// @brief Parses a specific ssh config file, collecting hosts.
    /// @param configPath Path to the ssh config file.
    /// @param results Output vector of query results.
    /// @param visited Set of canonical paths already processed (cycle guard).
    static void parseConfigFile(std::filesystem::path const& configPath,
                                std::vector<QueryResult>& results,
                                std::set<std::string>& visited);
};

} // namespace endo
