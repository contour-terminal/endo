// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/completion/CommandQueryProvider.hpp>
#include <shell/completion/PathCommandIndex.hpp>

#include <core/platform/ProcessEnvironment.hpp>

#include <string_view>
#include <vector>

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
    PathCommandQueryProvider(PathCommandIndex const& index, core::platform::ProcessEnvironment const& env);

    [[nodiscard]] std::vector<QueryResult> query(std::string_view queryTag) override;

  private:
    PathCommandIndex const& _index;
    core::platform::ProcessEnvironment const& _env;
};

} // namespace endo
