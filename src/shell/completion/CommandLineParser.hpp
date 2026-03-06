// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/completion/CommandSpec.hpp>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace endo
{

/// @brief The phase of completion the cursor is in.
enum class CompletionPhase // NOLINT(performance-enum-size)
{
    Subcommand,  ///< Completing a subcommand name.
    Option,      ///< Completing an option (--flag / -f).
    OptionValue, ///< Completing the value of an option that takes one.
    Argument,    ///< Completing a positional argument.
};

/// @brief Parsed state of a command line for completion purposes.
struct CommandLineState
{
    std::string command;                      ///< The base command (e.g., "git").
    std::vector<std::string> subcommandChain; ///< e.g., ["remote", "add"] for "git remote add".
    std::vector<std::string> positionalArgs;  ///< Completed positional args.
    std::vector<std::string> seenOptions;     ///< All options seen so far.
    std::string optionExpectingValue;         ///< Non-empty if cursor is on an option's value.
    CompletionPhase phase = CompletionPhase::Subcommand;
    size_t positionalArgIndex = 0; ///< Current positional argument index.
};

/// @brief Alias resolver function type for command aliases.
using AliasResolver = std::function<std::optional<std::string>(std::string_view)>;

/// @brief Parses a command line against a CommandSpec to determine completion state.
///
/// Understands global options (and which consume an argument), subcommand nesting,
/// and the difference between options and positional arguments.
///
/// @param spec The command specification to parse against.
/// @param fullInput The complete input line.
/// @param cursorPosition Cursor byte offset.
/// @param prefix The current prefix being typed.
/// @param aliasResolver Optional function to resolve aliases to canonical subcommand names.
/// @return Parsed command line state, or nullopt if not parseable for this spec.
[[nodiscard]] std::optional<CommandLineState> parseCommandLine(CommandSpec const& spec,
                                                               std::string_view fullInput,
                                                               size_t cursorPosition,
                                                               std::string_view prefix,
                                                               AliasResolver const& aliasResolver = {});

} // namespace endo
