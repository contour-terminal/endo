// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>

#include "FindExpression.hpp"
#include "StructuredCommand.hpp"

namespace endo
{

/// Built-in `find` command that searches directories for files matching an expression.
///
/// Returns results as a list of FileInfo records for structured F# pipeline use.
/// The FileInfo.name field contains the full relative path (matching GNU find output).
class FindCommand final: public StructuredCommand
{
  public:
    /// Constructs a FindCommand with parsed options and expression tree.
    /// @param options Parsed global options (search paths, depth limits, print mode).
    /// @param expression Parsed expression tree, or nullptr for default (match all).
    explicit FindCommand(find::FindOptions options, std::unique_ptr<find::Expr> expression);

    /// @return BuiltinTypeId::FileInfo
    [[nodiscard]] uint16_t outputTypeId() const override;

    /// Executes the find command, returning a list<FileInfo>.
    /// @param runner The VM runner for allocating objects.
    /// @return A cons-cell list of FileInfo TypedObject records.
    [[nodiscard]] CoreVM::TypedObject* execute(CoreVM::Runner& runner) const override;

  private:
    find::FindOptions _options;
    std::unique_ptr<find::Expr> _expression;
};

} // namespace endo
