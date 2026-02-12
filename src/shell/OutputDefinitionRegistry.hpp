// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "OutputDefinition.hpp"

namespace endo
{

/// Registry for output definition files that teach Endo how to parse external command output.
///
/// Loads YAML definition files (*.endo-output.yml) and provides lookup by command name + arguments.
class OutputDefinitionRegistry
{
  public:
    /// Loads all *.endo-output.yml files from a directory. Silently skips if directory doesn't exist.
    void loadFromDirectory(std::filesystem::path const& dir);

    /// Loads a single YAML definition file.
    /// @return true on success, false on parse error
    bool loadFromFile(std::filesystem::path const& path);

    /// Finds the best matching variant for the given command and arguments.
    /// @return Pointer to matching variant (owned by this registry), or nullptr if no match.
    [[nodiscard]] OutputVariant const* findMatch(std::string const& command,
                                                 std::vector<std::string> const& args) const;

    /// Returns all loaded variants across all definitions.
    [[nodiscard]] std::vector<OutputVariant const*> allVariants() const;

    /// Returns all loaded definitions.
    [[nodiscard]] std::vector<OutputDefinition> const& definitions() const noexcept { return _definitions; }

  private:
    /// Checks if a set of arguments matches a single pattern.
    [[nodiscard]] static bool matchesPattern(std::vector<std::string> const& args,
                                             std::vector<std::string> const& pattern);

    std::vector<OutputDefinition> _definitions;
};

} // namespace endo
