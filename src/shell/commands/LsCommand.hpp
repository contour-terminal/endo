// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

#include "StructuredCommand.hpp"
#include <platform/FileInfoProvider.hpp>

namespace endo
{

/// Built-in `ls` command that returns directory contents as a list of FileInfo records.
///
/// Uses a FileInfoProvider for platform abstraction and testability.
/// The provider is injected at construction, allowing mock providers in tests.
class LsCommand final: public StructuredCommand
{
  public:
    /// Constructs an LsCommand with the given file info provider and directory path.
    /// @param provider Platform-specific or mock file info provider.
    /// @param path Directory path to list (defaults to current directory).
    explicit LsCommand(FileInfoProvider const& provider, std::string path = ".");

    /// @return BuiltinTypeId::FileInfo
    [[nodiscard]] uint16_t outputTypeId() const override;

    /// Executes the ls command, returning a list<FileInfo>.
    /// @param runner The VM runner for allocating objects.
    /// @return A cons-cell list of FileInfo TypedObject records.
    [[nodiscard]] CoreVM::TypedObject* execute(CoreVM::Runner& runner) const override;

  private:
    FileInfoProvider const& _provider;
    std::string _path;
};

} // namespace endo
