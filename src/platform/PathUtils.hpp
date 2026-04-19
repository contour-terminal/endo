// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

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

/// @brief Resolves POSIX-style device paths to their platform-native equivalent.
///
/// Endo accepts POSIX device paths (`/dev/null`) across all platforms for portability.
/// On Windows these paths must be rewritten to the corresponding reserved device name
/// before being passed to file-open APIs:
///
/// | POSIX      | Windows |
/// |------------|---------|
/// | `/dev/null` | `NUL`   |
///
/// On POSIX platforms this helper is a no-op — device paths are native and require no
/// translation.
///
/// Apply this to **user-supplied** paths at the point they are opened (e.g. before
/// `std::ofstream`, `std::ifstream`, or low-level `open`/`CreateFileW` calls). Do not
/// apply it to generated or internal paths, as the mapping is intentionally limited to
/// the device names that users type.
///
/// @param path The path to resolve.
/// @return The platform-native path.
[[nodiscard]] inline auto resolveDevicePath(std::string_view path) -> std::string
{
#if defined(_WIN32)
    if (path == "/dev/null")
        return "NUL";
#endif
    return std::string { path };
}

} // namespace endo::platform
