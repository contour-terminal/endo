// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/completion/CommandQueryProvider.hpp>

#include <string_view>
#include <vector>

#include <platform/ProcessProvider.hpp>

namespace endo
{

/// @brief Provides completion candidates derived from the currently running
/// processes on the host system.
///
/// Used by the `pkill` builtin to suggest live process names. The provider
/// borrows a reference to a ProcessProvider injected by the completion system.
class ProcessNameQueryProvider: public CommandQueryProvider
{
  public:
    /// @param provider Reference to the process enumerator. Must outlive this object.
    explicit ProcessNameQueryProvider(ProcessProvider const& provider);

    /// @brief Resolves a query tag into completion candidates.
    ///
    /// Supported tags:
    ///  - `"process-names"` — deduplicated list of process command names.
    ///  - `"process-command-lines"` — same set today; reserved for future full-cmdline mode.
    ///
    /// @return Candidates (empty for unknown tags).
    [[nodiscard]] std::vector<QueryResult> query(std::string_view queryTag) override;

  private:
    ProcessProvider const& _provider;
};

} // namespace endo
