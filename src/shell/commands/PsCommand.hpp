// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "StructuredCommand.hpp"
#include <platform/ProcessProvider.hpp>

namespace endo
{

/// Built-in `ps` command that returns process information as a list of ProcessInfo records.
///
/// Uses a ProcessProvider for platform abstraction and testability.
/// The provider is injected at construction, allowing mock providers in tests.
class PsCommand final: public StructuredCommand
{
  public:
    /// Constructs a PsCommand with the given process provider.
    /// @param provider Platform-specific or mock process provider.
    explicit PsCommand(ProcessProvider const& provider);

    /// @return BuiltinTypeId::ProcessInfo
    [[nodiscard]] uint16_t outputTypeId() const override;

    /// Executes the ps command, returning a list<ProcessInfo>.
    /// @param runner The VM runner for allocating objects.
    /// @return A cons-cell list of ProcessInfo TypedObject records.
    [[nodiscard]] CoreVM::TypedObject* execute(CoreVM::Runner& runner) const override;

  private:
    ProcessProvider const& _provider;
};

} // namespace endo
