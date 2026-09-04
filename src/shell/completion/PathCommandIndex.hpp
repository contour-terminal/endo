// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <utility>
#include <vector>

#include <platform/EnvironmentProvider.hpp>
#include <platform/FileSystem.hpp>

namespace endo
{

/// @brief Enumerates the executables reachable through $PATH.
///
/// This is the one place that *lists* what $PATH offers; CommandResolver is the one place
/// that *resolves* a single name against it. Both go through FileSystem::isExecutableFile()
/// and CommandResolver::executableExtensions(), so they agree on what counts as executable —
/// without that, completion would offer names `which` cannot resolve, or hide ones it can.
///
/// Results are cached until the resolution-relevant environment changes ($PATH, plus
/// %PATHEXT% on Windows), matching CommandResolver's own cache key.
class PathCommandIndex
{
  public:
    /// @brief Constructs an index over @p env's $PATH, probing through @p fs.
    /// @param env Environment provider, used to read PATH and PATHEXT.
    /// @param fs  Filesystem abstraction, used to list directories and test executability.
    PathCommandIndex(EnvironmentProvider const& env, FileSystem const& fs);

    /// @brief Returns every executable on $PATH as (command name, resolved path), sorted by name.
    ///
    /// Names are unique: when several $PATH directories provide the same command, the first
    /// one wins, so the reported path is the one that would actually run.
    ///
    /// @return Reference to the cached entries; a later call re-scans when the environment
    ///         changed, so callers must not hold it across one.
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> const& entries() const;

  private:
    EnvironmentProvider const& _env;
    FileSystem const& _fs;

    // Mutable so that entries() can stay const while refreshing on demand.
    mutable std::vector<std::pair<std::string, std::string>> _entries;
    mutable std::string _cacheKey;

    /// @brief Scans every $PATH directory for executables.
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> scan() const;
};

} // namespace endo
