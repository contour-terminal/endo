// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <core/Environment.hpp>

#include <cstdint>
#include <string>
#include <string_view>

/// @brief Pure byte builders for the terminal shell-integration protocols.
///
/// Nothing here writes, remembers or reads global state: every function maps its arguments to
/// the bytes a terminal should receive. The stateful emitter lives on top of this layer.
namespace endo::shellintegration
{

/// @brief The semantic zones of a drawn prompt (OSC 133).
enum class PromptZone : std::uint8_t
{
    Start,        ///< Start of a new prompt (`A`).
    Prompt,       ///< A row of the primary prompt (`P;k=i`).
    Continuation, ///< A continuation row (`P;k=s`).
    RightPrompt,  ///< The right-aligned prompt (`P;k=r`).
    Input,        ///< The end of the prompt, where user input begins (`B`).
};

/// @brief Terminal-specific deviations from the shell-integration protocol.
struct TerminalQuirks
{
    /// The terminal mishandles `OSC 133 ; P`, so no `P` mark is sent at all.
    bool suppressPromptParts = false;
    /// The terminal mishandles `P;k=r`, so the right prompt is not marked.
    bool suppressRightPrompt = false;
    /// The shell runs inside tmux, which swallows sequences it does not know.
    bool insideTmux = false;
};

/// @brief Detects the quirks of the hosting terminal from its environment variables.
/// @param environment The environment to read; a variable counts when present and non-empty.
/// @return The quirks that apply.
[[nodiscard]] TerminalQuirks detectTerminalQuirks(core::Environment const& environment);

/// @brief Builds the OSC 133 sequence for a prompt zone.
/// @param zone The zone to mark.
/// @param quirks The quirks of the hosting terminal.
/// @return The sequence, or an empty string when @p quirks suppress it.
[[nodiscard]] std::string promptZoneSequence(PromptZone zone, TerminalQuirks const& quirks);

/// @brief Percent-encodes every byte except `[A-Za-z0-9-._~/]`, using uppercase hex.
/// @param bytes The bytes to encode.
/// @return The encoded text, which is pure ASCII.
[[nodiscard]] std::string percentEncode(std::string_view bytes);

/// @brief Builds the sequence marking the start of command output (`OSC 133 ; C`).
/// @param commandLine The command line being run; omitted from the sequence when empty.
/// @return The sequence, carrying the percent-encoded command line as `cmdline_url=`.
[[nodiscard]] std::string commandStartSequence(std::string_view commandLine);

/// @brief Builds the sequence marking the end of a command (`OSC 133 ; D`).
/// @param status The exit status of the command.
/// @return The sequence.
[[nodiscard]] std::string commandFinishedSequence(int status);

/// @brief Builds the working-directory report (`OSC 7`).
/// @param path The directory; it is normalized and turned into a `file://` URI.
/// @param hostName The name of the host the directory lives on.
/// @return The sequence.
[[nodiscard]] std::string currentDirectorySequence(std::string_view path, std::string_view hostName);

/// @brief Builds an iTerm2/WezTerm user-variable sequence (`OSC 1337 ; SetUserVar`).
/// @param name The variable name.
/// @param value The value, which is sent base64-encoded.
/// @return The sequence.
[[nodiscard]] std::string userVarSequence(std::string_view name, std::string_view value);

/// @brief Wraps a sequence in tmux's DCS passthrough, doubling every ESC inside it.
/// @param sequence The sequence to pass through to the outer terminal.
/// @return The wrapped sequence.
[[nodiscard]] std::string tmuxPassthrough(std::string_view sequence);

} // namespace endo::shellintegration
