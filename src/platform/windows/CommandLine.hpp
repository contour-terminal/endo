// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <span>
#include <string>
#include <string_view>

namespace endo::platform
{

/// @brief Quotes one argument for a Windows command line, so that the C runtime's parser (and
///        `CommandLineToArgvW`) reads it back unchanged.
///
/// Only spaces, tabs and quotes need it, and backslashes only where they precede a quote. No shell
/// is involved, so `&`, `|`, `;` and `%` are ordinary characters here.
/// @param arg The argument, as the program should receive it.
/// @return The argument as it is written on the command line.
[[nodiscard]] auto quoteArgument(std::string_view arg) -> std::string;

/// @brief Builds the UTF-16 command line `CreateProcessW` takes, from a program and its arguments.
/// @param program The program, as its `argv[0]`.
/// @param arguments The arguments after it, each quoted by @ref quoteArgument.
/// @return The command line, without a terminating NUL.
[[nodiscard]] auto buildCommandLine(std::string_view program, std::span<std::string const> arguments)
    -> std::wstring;

} // namespace endo::platform
