// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <CoreVM/types/TypedObject.hpp>

#include <cstdint>

namespace CoreVM
{
class Runner;
}

namespace endo
{

/// Interface for commands that produce structured (typed record) output.
///
/// Structured commands return their results as typed objects (records)
/// instead of text streams, enabling direct pipeline composition with
/// filter, map, sortBy, etc.
class StructuredCommand
{
  public:
    virtual ~StructuredCommand() = default;

    /// Returns the well-known type ID for the output record type.
    [[nodiscard]] virtual uint16_t outputTypeId() const = 0;

    /// Executes the command and returns results as a cons-cell list of typed records.
    /// @param runner The VM runner for allocating objects.
    /// @return A list (cons-cell chain) of TypedObject records, or Nil for empty results.
    [[nodiscard]] virtual CoreVM::TypedObject* execute(CoreVM::Runner& runner) const = 0;
};

} // namespace endo
