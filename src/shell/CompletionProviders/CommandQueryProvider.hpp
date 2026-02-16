// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace endo
{

/// @brief A completion candidate from a dynamic query.
struct QueryResult
{
    std::string text;        ///< The completion text.
    std::string description; ///< Optional description shown in menu.
};

/// @brief Abstract interface for providing dynamic completion data.
///
/// Each command (git, docker, etc.) implements this to resolve queryTag
/// strings into actual completion candidates by running external commands.
class CommandQueryProvider
{
  public:
    virtual ~CommandQueryProvider() = default;

    /// @brief Resolves a query tag into completion candidates.
    /// @param queryTag The tag from OptionDef/ArgDef (e.g., "branches", "remotes").
    /// @return List of candidates, or empty if tag is unknown.
    [[nodiscard]] virtual std::vector<QueryResult> query(std::string_view queryTag) = 0;
};

} // namespace endo
