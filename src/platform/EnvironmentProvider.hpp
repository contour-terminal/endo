// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <platform/PlatformError.hpp>

namespace endo::platform
{

/// @brief Abstract interface for environment variables and working directory operations.
///
/// Consolidates environment variable access with working directory management
/// into a single abstraction, enabling fully isolated test environments.
class EnvironmentProvider
{
  public:
    virtual ~EnvironmentProvider() = default;

    /// Sets an environment variable.
    ///
    /// @param name  Variable name
    /// @param value Variable value
    virtual void set(std::string_view name, std::string_view value) = 0;

    /// Retrieves an environment variable's value.
    ///
    /// Returns an owned copy to avoid dangling references (the Windows implementation
    /// uses a shared buffer that is overwritten on each call).
    ///
    /// @param name Variable name
    /// @return The value if set, or std::nullopt
    [[nodiscard]] virtual std::optional<std::string> get(std::string_view name) const = 0;

    /// Removes an environment variable.
    ///
    /// @param name Variable name to remove
    virtual void unset(std::string_view name) = 0;

    /// Exports a variable to child processes.
    ///
    /// @param name Variable name to export
    virtual void exportVariable(std::string_view name) = 0;

    /// Returns all variable names currently defined (local and exported).
    [[nodiscard]] virtual std::vector<std::string> keys() const = 0;

    /// Convenience: sets a variable and immediately exports it.
    ///
    /// @param name  Variable name
    /// @param value Variable value
    void setAndExport(std::string_view name, std::string_view value)
    {
        set(name, value);
        exportVariable(name);
    }

    /// Changes the current working directory.
    ///
    /// @param path New working directory path
    /// @return Success or an error
    [[nodiscard]] virtual std::expected<void, PlatformError> changeDirectory(
        std::filesystem::path const& path) = 0;

    /// Returns the current working directory.
    [[nodiscard]] virtual std::string currentDirectory() const = 0;

    /// @brief Returns the user's home directory path.
    ///
    /// Tries HOME (Unix), then USERPROFILE (Windows).
    ///
    /// @return The home directory path, or std::nullopt if neither variable is set.
    [[nodiscard]] auto homeDirectory() const -> std::optional<std::filesystem::path>
    {
        if (auto home = get("HOME"))
            return std::filesystem::path(*home);
        if (auto home = get("USERPROFILE"))
            return std::filesystem::path(*home);
        return std::nullopt;
    }

    /// @brief Returns the user's configuration base directory.
    ///
    /// On Unix: XDG_CONFIG_HOME or ~/.config.
    /// On Windows: APPDATA (typically ~/AppData/Roaming).
    ///
    /// @return The configuration directory path, or std::nullopt if it cannot be determined.
    [[nodiscard]] auto configHome() const -> std::optional<std::filesystem::path>
    {
        if (auto xdg = get("XDG_CONFIG_HOME"); xdg && !xdg->empty())
            return std::filesystem::path(*xdg);
        if (auto appdata = get("APPDATA"); appdata && !appdata->empty())
            return std::filesystem::path(*appdata);
        if (auto home = homeDirectory())
            return *home / ".config";
        return std::nullopt;
    }
};

} // namespace endo::platform

// Backward-compatible alias in the endo namespace
namespace endo
{
using endo::platform::EnvironmentProvider;
} // namespace endo
