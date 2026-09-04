// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/completion/CommandQueryProvider.hpp>
#include <shell/completion/PathCommandIndex.hpp>

#include <string_view>
#include <vector>

#include <platform/EnvironmentProvider.hpp>

namespace endo
{

/// @brief Serves the "path-commands" query tag: the names of executables on $PATH.
///
/// Used for positional arguments that name a program rather than a file — `which` being
/// the motivating case. Each candidate carries its resolved path as the description, so
/// the menu shows where the command would come from.
class PathCommandQueryProvider: public CommandQueryProvider
{
  public:
    /// @brief Constructs a provider backed by @p index.
    /// @param index The shared $PATH index; must outlive this provider.
    /// @param env   Environment provider, used to shorten paths for display.
    PathCommandQueryProvider(PathCommandIndex const& index, EnvironmentProvider const& env);

    [[nodiscard]] std::vector<QueryResult> query(std::string_view queryTag) override;

  private:
    PathCommandIndex const& _index;
    EnvironmentProvider const& _env;
};

} // namespace endo
