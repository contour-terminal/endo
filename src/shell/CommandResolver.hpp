// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <platform/EnvironmentProvider.hpp>

#include <set>
#include <string>
#include <string_view>
#include <unordered_map>

namespace endo
{

/// @brief Type of command resolved.
enum class CommandType
{
    External, ///< Executable found in $PATH.
    Builtin,  ///< Shell builtin command.
    Alias,    ///< Shell alias (placeholder for future).
    NotFound, ///< Command not found.
};

/// @brief Information about a resolved command.
struct CommandInfo
{
    CommandType type = CommandType::NotFound;
    std::string tooltip; ///< Tooltip text (path, "shell builtin", etc.)
};

/// @brief Resolves commands to their type and path for tooltip display.
///
/// CommandResolver determines whether a command is:
/// - An external executable (returns full path)
/// - A shell builtin (returns "shell builtin")
/// - An alias (placeholder, returns "alias: <expansion>")
/// - Not found (returns "command not found")
///
/// It caches PATH lookups for efficiency.
class CommandResolver
{
  public:
    /// @brief Constructs a resolver with access to the environment.
    explicit CommandResolver(EnvironmentProvider const& env);

    /// @brief Resolves a command and returns its info.
    /// @param command The command name to resolve.
    /// @return CommandInfo with type and tooltip text.
    [[nodiscard]] CommandInfo resolve(std::string_view command) const;

    /// @brief Invalidates the PATH cache.
    ///
    /// Call this when $PATH changes to force re-scanning.
    void invalidateCache();

  private:
    EnvironmentProvider const& _env;

    // Cache for efficiency
    mutable std::string _cachedPath;
    mutable std::unordered_map<std::string, std::string> _pathCache; // command -> full path

    /// @brief Searches $PATH for an executable.
    [[nodiscard]] std::string findInPath(std::string_view command) const;

    /// @brief Refreshes the PATH cache if $PATH has changed.
    void refreshCacheIfNeeded() const;

    /// @brief Returns the list of builtin command names.
    [[nodiscard]] static std::set<std::string> const& builtinNames();
};

} // namespace endo
