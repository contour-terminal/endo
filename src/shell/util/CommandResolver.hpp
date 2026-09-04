// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <platform/EnvironmentProvider.hpp>
#include <platform/FileSystem.hpp>

namespace endo
{

/// @brief Type of command resolved.
enum class CommandType // NOLINT(performance-enum-size)
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
    /// @brief Constructs a resolver with access to the environment and filesystem.
    /// @param env Environment provider, used to read PATH and PATHEXT.
    /// @param fs  Filesystem abstraction, used to test candidate executables.
    CommandResolver(EnvironmentProvider const& env, FileSystem const& fs);

    /// @brief Resolves a command and returns its info.
    /// @param command The command name to resolve.
    /// @return CommandInfo with type and tooltip text.
    [[nodiscard]] CommandInfo resolve(std::string_view command) const;

    /// @brief Invalidates the PATH cache.
    ///
    /// Call this when $PATH changes to force re-scanning.
    void invalidateCache();

    /// @brief Returns the list of builtin command names.
    [[nodiscard]] static std::set<std::string> const& builtinNames();

    /// @brief Returns the file extensions that make a $PATH entry executable.
    ///
    /// On Windows this is %PATHEXT% (lower-cased, blank tokens dropped), falling back to
    /// a built-in default list when it is unset, empty or all-blank. On POSIX it is empty:
    /// executability there comes from the permission bits, not the name.
    ///
    /// Exposed so that callers which *enumerate* $PATH (completion) apply the same rule as
    /// the resolution performed here; two different notions of "executable" would let
    /// completion offer names that cannot be resolved, or hide ones that can.
    ///
    /// @param env Environment provider, used to read PATHEXT on Windows.
    /// @return Lower-cased extensions including the leading dot, or empty on POSIX.
    [[nodiscard]] static std::vector<std::string> executableExtensions(EnvironmentProvider const& env);

    /// @brief Searches $PATH for the first executable matching @p command.
    ///
    /// On POSIX, also verifies execute permission bits.
    /// On Windows, applies PATHEXT (see @ref candidateNames for the exact rule).
    /// Stops at the first match.
    ///
    /// @param command  Bare command name (no path separators).
    /// @return Full path string if found, empty string if not found.
    [[nodiscard]] std::string findInPath(std::string_view command) const;

    /// @brief Searches $PATH for all executables matching @p command.
    ///
    /// Same logic as findInPath() but collects every match across all PATH directories.
    ///
    /// @param command  Bare command name (no path separators).
    /// @return Vector of full path strings for every match (may be empty).
    [[nodiscard]] std::vector<std::string> findAllInPath(std::string_view command) const;

  private:
    EnvironmentProvider const& _env;
    FileSystem const& _fs;

    // Cache for efficiency. The key combines $PATH and (on Windows) $PATHEXT, since both
    // govern resolution; a change to either invalidates the per-command results.
    mutable std::string _cacheKey;
    mutable std::unordered_map<std::string, std::string> _pathCache; // command -> full path

    /// @brief Refreshes the PATH cache if $PATH (or, on Windows, $PATHEXT) has changed.
    void refreshCacheIfNeeded() const;

    /// @brief Computes the cache key from the resolution-relevant environment.
    /// @return $PATH on POSIX; "$PATH\\x1f$PATHEXT" on Windows.
    [[nodiscard]] std::string computeCacheKey() const;

    /// @brief Searches $PATH for executables matching @p command.
    /// @param command   Bare command name (no path separators).
    /// @param firstOnly Stop after the first match (used by findInPath()).
    /// @return Full path strings for the matches found (may be empty).
    [[nodiscard]] std::vector<std::string> search(std::string_view command, bool firstOnly) const;

    /// @brief Builds the candidate file names (relative to each PATH directory) for @p command.
    ///
    /// POSIX: always just @p command itself.
    /// Windows (cmd.exe / PowerShell semantics):
    ///   - if @p command already carries an extension, it is used verbatim and PATHEXT is
    ///     not applied (an explicitly typed name is never augmented);
    ///   - otherwise the bare name is expanded to `command + ext` for each PATHEXT entry and
    ///     the extensionless name is never probed — this is what stops an extensionless
    ///     `docker` shim from shadowing the real `docker.exe`.
    ///
    /// @param env     Environment provider, used to read PATHEXT on Windows (unused on POSIX).
    /// @param command Bare command name.
    /// @return Ordered list of candidate file names.
    [[nodiscard]] static std::vector<std::string> candidateNames(EnvironmentProvider const& env,
                                                                 std::string_view command);
};

} // namespace endo
