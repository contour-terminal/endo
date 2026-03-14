// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file InlineArgParser.hpp
/// @brief Shared POSIX-style argument parser for inline builtin commands.

#include <shell/builtins/InlineCommandDescriptor.hpp>

#include <CoreVM/CoreVM.hpp>

#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace endo
{

/// @brief Result of parsing args for an inline builtin.
struct ParsedInlineArgs
{
    bool helpRequested = false; ///< -h or --help was given

    /// @brief Parsed flag entries. The key is the canonical short flag (e.g., "-r")
    /// or long flag if no short form exists. The value is the flag's argument
    /// (empty string_view for boolean flags).
    std::vector<std::pair<std::string_view, std::string>> flags;

    /// @brief Non-flag positional arguments.
    std::vector<std::string> positionalArgs;

    /// @brief Check whether a boolean flag was given.
    [[nodiscard]] bool hasFlag(std::string_view flag) const;

    /// @brief Get the value of a value-taking flag, or std::nullopt if not given.
    [[nodiscard]] std::optional<std::string_view> getFlagValue(std::string_view flag) const;
};

/// @brief Parses args according to option definitions.
///
/// Handles: combined short flags (-rfv), --flag=value, -h/--help,
/// end-of-options (--), and value-taking flags (-n NUM).
/// Skips args[0] (the command name).
///
/// @param args The full argument array including command name at index 0.
/// @param options The option definitions from the command descriptor.
/// @return The parsed result; caller checks helpRequested first.
[[nodiscard]] ParsedInlineArgs parseInlineArgs(CoreVM::CoreStringArray const& args,
                                               std::span<InlineOptionDef const> options);

} // namespace endo
