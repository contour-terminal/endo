// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <string>

namespace endo::platform
{

/// @brief Normalizes a path string to use forward slashes.
///
/// On Windows, replaces all backslashes with forward slashes.
/// Preserves UNC paths by normalizing `\\server\share` to `//server/share`.
/// No-op on POSIX platforms.
///
/// @param path The path string to normalize
/// @return The normalized path string
[[nodiscard]] inline auto normalizePath(std::string path) -> std::string
{
#if defined(_WIN32)
    std::ranges::replace(path, '\\', '/');
#endif
    return path;
}

/// @brief Normalizes a filesystem path to use forward slashes.
///
/// Convenience overload that converts a std::filesystem::path to a normalized string.
///
/// @param p The filesystem path to normalize
/// @return The normalized path string
[[nodiscard]] inline auto normalizePath(std::filesystem::path const& p) -> std::string
{
    return p.generic_string();
}

} // namespace endo::platform
