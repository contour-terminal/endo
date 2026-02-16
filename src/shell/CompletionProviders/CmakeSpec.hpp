// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/CompletionProviders/CommandQueryProvider.hpp>
#include <shell/CompletionProviders/CommandSpec.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace endo
{

/// @brief Creates the cmake CommandSpec with global options and preset completion.
///
/// Models cmake's modes (--build, --install) as flag options and --preset
/// as a global option with DynamicQuery since preset names apply across modes.
[[nodiscard]] CommandSpec createCmakeSpec();

/// @brief Creates the ctest CommandSpec with preset completion and test options.
[[nodiscard]] CommandSpec createCtestSpec();

/// @brief Query provider for cmake/ctest preset names.
///
/// Reads CMakePresets.json and CMakeUserPresets.json from the current directory,
/// recursively resolving "include" arrays and filtering presets by platform "condition"
/// (including conditions inherited via the "inherits" chain).
/// Resolves queryTag: "presets".
class CmakeQueryProvider: public CommandQueryProvider
{
  public:
    [[nodiscard]] std::vector<QueryResult> query(std::string_view queryTag) override;
};

} // namespace endo
