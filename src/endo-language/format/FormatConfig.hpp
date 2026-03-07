// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>

namespace endo::format
{

/// Configuration for the Endo source code formatter.
///
/// Controls indentation, line width, and spacing. Can be loaded from a `.endo-format` YAML file
/// or constructed with defaults.
struct FormatConfig
{
    uint32_t indentWidth = 4;               ///< Number of spaces (or tab width) per indent level
    bool useSpaces = true;                  ///< True for spaces, false for tabs
    uint32_t maxLineWidth = 100;            ///< Target maximum line width for line-break decisions
    bool trailingNewline = true;            ///< Ensure file ends with a newline
    uint32_t blankLinesBetweenTopLevel = 1; ///< Blank lines between top-level statements

    /// Loads configuration from a YAML file.
    /// @param path Path to the `.endo-format` YAML file.
    /// @return The parsed config, or an error message.
    [[nodiscard]] static std::expected<FormatConfig, std::string> load(std::filesystem::path const& path);

    /// Searches for a `.endo-format` config file starting from the given directory,
    /// walking up the directory tree toward the filesystem root.
    /// @param startDir Directory to start searching from.
    /// @return Path to the found config file, or std::nullopt.
    [[nodiscard]] static std::optional<std::filesystem::path> findConfigFile(
        std::filesystem::path const& startDir);

    /// Returns the indent string for one level based on current settings.
    [[nodiscard]] std::string indentString() const;
};

} // namespace endo::format
