// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "JobProvider.hpp"
#include "StructuredCommand.hpp"

namespace endo
{

/// Built-in `jobs` command that returns background job information as a list of JobInfo records.
///
/// Uses a JobProvider for abstraction and testability.
/// The provider is injected at construction, allowing mock providers in tests.
class JobsCommand final: public StructuredCommand
{
  public:
    /// Constructs a JobsCommand with the given job provider.
    /// @param provider Shell or mock job provider.
    explicit JobsCommand(JobProvider const& provider);

    /// @return BuiltinTypeId::JobInfo
    [[nodiscard]] uint16_t outputTypeId() const override;

    /// Executes the jobs command, returning a list<JobInfo>.
    /// @param runner The VM runner for allocating objects.
    /// @return A cons-cell list of JobInfo TypedObject records.
    [[nodiscard]] CoreVM::TypedObject* execute(CoreVM::Runner& runner) const override;

  private:
    JobProvider const& _provider;
};

} // namespace endo
