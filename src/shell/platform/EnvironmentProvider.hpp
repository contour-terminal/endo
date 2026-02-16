// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/Error.hpp>

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace endo
{

/// @brief Abstract interface for environment variables and working directory operations.
///
/// Consolidates environment variable access (formerly Environment) with
/// working directory management (formerly on ProcessManager) into a single
/// abstraction, enabling fully isolated test environments.
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
    /// @param name Variable name
    /// @return The value if set, or std::nullopt
    [[nodiscard]] virtual std::optional<std::string_view> get(std::string_view name) const = 0;

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
    inline void setAndExport(std::string_view name, std::string_view value)
    {
        set(name, value);
        exportVariable(name);
    }

    /// Changes the current working directory.
    ///
    /// @param path New working directory path
    /// @return Success or an error
    [[nodiscard]] virtual std::expected<void, ShellError> changeDirectory(
        std::filesystem::path const& path) = 0;

    /// Returns the current working directory.
    [[nodiscard]] virtual std::string currentDirectory() const = 0;
};

} // namespace endo
