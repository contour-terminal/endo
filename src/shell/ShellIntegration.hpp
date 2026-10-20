// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <core/Environment.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "ContextSignalling.hpp"
#include "ShellIntegrationSequences.hpp"

namespace endo
{

class TTY;

/// @brief The stateful shell-integration emitter: tells the terminal about prompts, commands,
///        the working directory and the shell session.
///
/// Speaks OSC 133 (semantic prompt marks), OSC 7 (working directory), OSC 3008 (context
/// signalling) and OSC 1337 SetUserVar. Every automatic write requires an interactive shell,
/// a terminal on stdout, and the integration being enabled (`shell_integration`). Inside tmux,
/// OSC 3008 is not sent and SetUserVar is wrapped in tmux's DCS passthrough.
class ShellIntegration
{
  public:
    /// @brief Who and where the shell is, as announced in the shell context.
    struct Identity
    {
        std::string user;                     ///< Login name of the user.
        std::string hostName;                 ///< Host name, also used in OSC 7 URIs.
        std::optional<std::string> machineId; ///< Machine id, if the platform has one.
        int pid = 0;                          ///< Process id of the shell.
    };

    /// @brief Constructs the emitter.
    /// @param tty The terminal to write to; it must outlive the emitter.
    /// @param environment The environment the terminal quirks are detected from.
    /// @param identity The identity announced in the shell context.
    /// @param random Source of random 64-bit values for context ids.
    ShellIntegration(TTY& tty,
                     core::Environment const& environment,
                     Identity identity,
                     std::function<std::uint64_t()> random);

    /// @brief Sets whether the shell runs interactively; nothing automatic is written otherwise.
    /// @param interactive Whether the shell is interactive.
    void setInteractive(bool interactive) noexcept;

    /// @brief Turns the integration on or off.
    ///
    /// Turning it off ends the open shell context; turning it on starts a new shell context, with
    /// a new id, at the next prompt. A command running while the integration is toggled is
    /// finished as it was started: with its command context's end and `133;D` if its `133;C`
    /// was sent, with neither otherwise.
    /// @param enabled Whether the integration is enabled.
    void setEnabled(bool enabled);

    /// @brief Returns whether the integration is enabled.
    [[nodiscard]] bool enabled() const noexcept { return _enabled; }

    /// @brief Returns the quirks of the hosting terminal.
    [[nodiscard]] shellintegration::TerminalQuirks const& quirks() const noexcept { return _quirks; }

    /// @brief Builds the OSC 133 mark for a prompt zone.
    /// @param zone The zone to mark.
    /// @return The sequence, or an empty string when nothing may be written.
    [[nodiscard]] std::string promptZoneSequence(shellintegration::PromptZone zone) const;

    /// @brief Reports the working directory (OSC 7) before a prompt is drawn.
    /// @param cwd The working directory.
    void beforePrompt(std::string_view cwd);

    /// @brief Announces the shell context (OSC 3008, full field set) and the per-prompt user
    ///        variables after a prompt has been drawn.
    /// @param cwd The working directory.
    void afterPromptDrawn(std::string_view cwd);

    /// @brief Marks the start of command output (`133;C`), opens the command context and
    ///        updates `WEZTERM_PROG`.
    /// @param commandLine The command line being run.
    /// @param cwd The working directory it runs in.
    void commandStarted(std::string_view commandLine, std::string_view cwd);

    /// @brief Closes the command context and marks the end of the command (`133;D`).
    ///
    /// Writes only when commandStarted() sent `133;C` for the command, whether or not the
    /// integration is still enabled.
    /// @param outcome The outcome of the command.
    void commandFinished(shellintegration::CommandOutcome const& outcome);

    /// @brief Reports a changed working directory (OSC 7), e.g. after `cd`.
    /// @param cwd The new working directory.
    void workingDirectoryChanged(std::string_view cwd);

    /// @brief Ends the shell context when the interactive session ends.
    /// @param status The exit status of the shell.
    void sessionEnded(int status);

    /// @brief Sets a terminal user variable on explicit request.
    ///
    /// Writes whenever stdout is a terminal, regardless of interactivity or of the integration
    /// being enabled.
    /// @param name The variable name.
    /// @param value The value.
    void setUserVar(std::string_view name, std::string_view value) const;

  private:
    /// The protocol a sequence belongs to; decides how it travels through tmux.
    enum class Channel : std::uint8_t
    {
        PromptMark, ///< OSC 133.
        Directory,  ///< OSC 7.
        Context,    ///< OSC 3008.
        UserVar,    ///< OSC 1337 SetUserVar.
    };

    [[nodiscard]] bool active() const noexcept;
    void write(Channel channel, std::string_view sequence) const;
    [[nodiscard]] bool sends(Channel channel) const noexcept;
    void endShellContext();

    TTY& _tty;
    shellintegration::TerminalQuirks _quirks;
    Identity _identity;
    std::function<std::uint64_t()> _random;
    bool _interactive = false;
    bool _enabled = true;
    std::optional<std::string> _shellContextId;
    std::optional<std::string> _commandContextId;
    bool _commandOpen = false; ///< Whether `133;C` was sent for the running command.
    std::string _lastProg;
};

} // namespace endo
