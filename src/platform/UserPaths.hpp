// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdlib>
#include <filesystem>
#include <optional>

namespace endo::platform
{

/// @brief Returns the user's home directory.
///
/// Tries HOME (Unix), then USERPROFILE (Windows).
///
/// @return The home directory path, or std::nullopt if neither variable is set.
[[nodiscard]] inline auto homeDirectory() -> std::optional<std::filesystem::path>
{
    if (auto const* home = std::getenv("HOME"))
        return std::filesystem::path(home);
    if (auto const* home = std::getenv("USERPROFILE"))
        return std::filesystem::path(home);
    return std::nullopt;
}

/// @brief Returns the user's configuration base directory.
///
/// On Unix: $XDG_CONFIG_HOME, or ~/.config if not set.
/// On Windows: $APPDATA (typically ~/AppData/Roaming).
///
/// @return The configuration directory path, or std::nullopt if it cannot be determined.
[[nodiscard]] inline auto configHome() -> std::optional<std::filesystem::path>
{
    if (auto const* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg != '\0')
        return std::filesystem::path(xdg);
    if (auto const* appdata = std::getenv("APPDATA"); appdata && *appdata != '\0')
        return std::filesystem::path(appdata);
    if (auto home = homeDirectory())
        return *home / ".config";
    return std::nullopt;
}

/// @brief Returns the user's cache base directory.
///
/// On Unix: $XDG_CACHE_HOME, or ~/.cache if not set.
/// On Windows: $LOCALAPPDATA (typically ~/AppData/Local), which is the
/// machine-local (non-roaming) counterpart to $APPDATA — the right home for
/// regenerable cache data.
///
/// @return The cache directory path, or std::nullopt if it cannot be determined.
[[nodiscard]] inline auto cacheHome() -> std::optional<std::filesystem::path>
{
    if (auto const* xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg != '\0')
        return std::filesystem::path(xdg);
    if (auto const* localAppData = std::getenv("LOCALAPPDATA"); localAppData && *localAppData != '\0')
        return std::filesystem::path(localAppData);
    if (auto home = homeDirectory())
        return *home / ".cache";
    return std::nullopt;
}

} // namespace endo::platform
