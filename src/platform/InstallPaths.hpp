// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace endo::platform
{

/// @brief Returns the path to the currently running executable.
/// @return The executable's filesystem path, or nullopt if it cannot be determined.
[[nodiscard]] auto executablePath() -> std::optional<std::filesystem::path>;

/// @brief Resolves an endo data directory relative to the running executable's install prefix.
///
/// Computes <exe_dir>/../share/endo/<subdir> based on the executable's location.
/// This follows the standard FHS layout where binaries are in <prefix>/bin/ and
/// data files are in <prefix>/share/.
///
/// @param subdir The subdirectory within the data prefix (e.g., "definitions", "completers").
/// @return The resolved path, or empty path if the executable path cannot be determined.
[[nodiscard]] auto resolveDataDir(std::string_view subdir) -> std::filesystem::path;

} // namespace endo::platform
