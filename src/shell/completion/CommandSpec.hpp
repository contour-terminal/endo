// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

namespace endo
{

/// @brief What kind of value an option expects.
enum class OptionValueKind // NOLINT(performance-enum-size)
{
    None,         ///< Boolean flag, no value.
    String,       ///< Free-form string value.
    Path,         ///< File/directory path (delegates to FileCompleter).
    Enum,         ///< One of a fixed set of values.
    DynamicQuery, ///< Resolved via CommandQueryProvider at completion time.
};

/// @brief What kind of value a positional argument expects.
enum class ArgKind // NOLINT(performance-enum-size)
{
    Any,          ///< No special completion (free text).
    Path,         ///< File/directory path (delegates to FileCompleter).
    Subcommand,   ///< Nested subcommand (e.g., "git stash list").
    DynamicQuery, ///< Resolved via CommandQueryProvider at completion time.
};

/// @brief Definition of a command option/flag.
struct OptionDef
{
    std::string longName;    ///< e.g., "--force" (empty if none).
    std::string shortName;   ///< e.g., "-f" (empty if none).
    std::string description; ///< Human-readable help text.
    OptionValueKind valueKind = OptionValueKind::None;
    std::vector<std::string> enumValues; ///< When valueKind == Enum.
    std::string queryTag;                ///< When valueKind == DynamicQuery, identifies the query.
};

/// @brief Definition of a positional argument slot.
struct ArgDef
{
    ArgKind kind = ArgKind::Any; ///< What kind of completion to offer.
    std::string description;     ///< Human-readable help text.
    std::string queryTag;        ///< When kind == DynamicQuery, identifies the query.
    bool repeatable = false;     ///< If true, repeats for all subsequent positions.

    /// When a seen option matches, override queryTag with the alternative.
    /// E.g., {"-d", "local-branches"} uses "local-branches" instead of "branches" when -d is present.
    std::vector<std::pair<std::string, std::string>> optionQueryOverrides;
};

/// @brief Complete definition of a subcommand.
struct SubcommandDef
{
    std::string name;                       ///< Subcommand name (e.g., "checkout").
    std::string description;                ///< Human-readable help text.
    std::vector<OptionDef> options;         ///< Options specific to this subcommand.
    std::vector<ArgDef> positionalArgs;     ///< Positional argument definitions.
    std::vector<SubcommandDef> subcommands; ///< Nested subcommands (e.g., "git remote add").
};

/// @brief Top-level command specification.
///
/// Describes a command's subcommands, options, and positional arguments
/// in a data-driven format for the completion system.
struct CommandSpec
{
    std::string command;                    ///< Command name (e.g., "git").
    std::string description;                ///< Human-readable help text.
    std::vector<OptionDef> globalOptions;   ///< Options valid before any subcommand.
    std::vector<SubcommandDef> subcommands; ///< Available subcommands.
    std::vector<ArgDef> positionalArgs;     ///< For commands without subcommands.
};

} // namespace endo
