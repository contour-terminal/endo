// SPDX-License-Identifier: Apache-2.0
#include "ShellIntegration.hpp"

#include <array>
#include <utility>
#include <vector>

#include "TTY.hpp"

namespace endo
{

using namespace shellintegration;

namespace
{

    /// How a protocol's sequences travel when the shell runs inside tmux.
    enum class TmuxTreatment : std::uint8_t
    {
        Plain,       ///< tmux understands the sequence itself.
        Passthrough, ///< Wrapped in tmux's DCS passthrough for the outer terminal.
        Drop,        ///< Not sent at all.
    };

    /// tmux treatment per channel, indexed by the channel's value.
    constexpr auto TmuxTreatments = std::array {
        TmuxTreatment::Plain,       // PromptMark: tmux interprets OSC 133 itself.
        TmuxTreatment::Plain,       // Directory: tmux interprets OSC 7 itself.
        TmuxTreatment::Drop,        // Context: OSC 3008 alone would derive marks at tmux's cursor.
        TmuxTreatment::Passthrough, // UserVar: only the outer terminal knows SetUserVar.
    };

    /// Longest command line, in bytes, sent whole in `133;C` (`cmdline_url=`) and `WEZTERM_PROG`.
    constexpr std::size_t MaxRepeatedCommandLineSize = 16 * 1024;

} // namespace

ShellIntegration::ShellIntegration(TTY& tty,
                                   core::Environment const& environment,
                                   Identity identity,
                                   std::function<std::uint64_t()> random):
    _tty { tty },
    _quirks { detectTerminalQuirks(environment) },
    _identity { std::move(identity) },
    _random { std::move(random) }
{
}

void ShellIntegration::setInteractive(bool interactive) noexcept
{
    _interactive = interactive;
}

void ShellIntegration::setEnabled(bool enabled)
{
    if (enabled == _enabled)
        return;
    if (!enabled)
        endShellContext();
    _enabled = enabled;
}

std::string ShellIntegration::promptZoneSequence(PromptZone zone) const
{
    if (!active())
        return {};
    return shellintegration::promptZoneSequence(zone, _quirks);
}

void ShellIntegration::beforePrompt(std::string_view cwd)
{
    workingDirectoryChanged(cwd);
}

void ShellIntegration::afterPromptDrawn(std::string_view cwd)
{
    if (!active())
        return;

    if (sends(Channel::Context))
    {
        if (!_shellContextId)
            _shellContextId = newContextId(_random);

        // A repeated start= resets every omitted field, so each re-announce carries them all.
        auto fields = std::vector<ContextField> { { .key = "type", .value = "shell" } };
        if (!_identity.user.empty())
            fields.push_back({ .key = "user", .value = _identity.user });
        fields.push_back({ .key = "hostname", .value = _identity.hostName });
        if (_identity.machineId)
            fields.push_back({ .key = "machineid", .value = *_identity.machineId });
        fields.push_back({ .key = "pid", .value = std::to_string(_identity.pid) });
        fields.push_back({ .key = "comm", .value = "endo" });
        fields.push_back({ .key = "cwd", .value = std::string(cwd) });
        write(Channel::Context, contextStartSequence(*_shellContextId, fields));
    }

    setUserVar("WEZTERM_USER", _identity.user);
    setUserVar("WEZTERM_HOST", _identity.hostName);
    setUserVar("WEZTERM_IN_TMUX", _quirks.insideTmux ? "1" : "0");
    if (!_lastProg.empty())
    {
        _lastProg.clear();
        setUserVar("WEZTERM_PROG", "");
    }
}

void ShellIntegration::commandStarted(std::string_view commandLine, std::string_view cwd)
{
    _commandOpen = active();
    if (!_commandOpen)
        return;

    // An oversized command line is not repeated in full into 133;C and WEZTERM_PROG; OSC 3008's
    // cmdline= still carries it, truncated.
    auto const fitsInSequences = commandLine.size() <= MaxRepeatedCommandLineSize;

    write(Channel::PromptMark, commandStartSequence(fitsInSequences ? commandLine : std::string_view {}));

    if (sends(Channel::Context))
    {
        _commandContextId = newContextId(_random);
        auto const fields = std::array {
            ContextField { .key = "type", .value = "command" },
            ContextField { .key = "cmdline", .value = std::string(commandLine) },
            ContextField { .key = "cwd", .value = std::string(cwd) },
        };
        write(Channel::Context, contextStartSequence(*_commandContextId, fields));
    }

    if (fitsInSequences && commandLine != _lastProg)
    {
        _lastProg = commandLine;
        setUserVar("WEZTERM_PROG", commandLine);
    }
}

void ShellIntegration::commandFinished(CommandOutcome const& outcome)
{
    // Pairs with the 133;C of commandStarted, not with the current state: the command may have
    // toggled the integration itself (`shell_integration <- false`).
    if (!std::exchange(_commandOpen, false))
        return;

    if (auto const id = std::exchange(_commandContextId, std::nullopt); id)
        write(Channel::Context, contextEndSequence(*id, commandEndFields(outcome)));
    write(Channel::PromptMark, commandFinishedSequence(outcome.status));
}

void ShellIntegration::workingDirectoryChanged(std::string_view cwd)
{
    if (active())
        write(Channel::Directory, currentDirectorySequence(cwd, _identity.hostName));
}

void ShellIntegration::sessionEnded(int status)
{
    if (!active())
        return;
    if (auto const id = std::exchange(_shellContextId, std::nullopt); id)
    {
        auto const fields = std::array {
            ContextField { .key = "exit", .value = "success" },
            ContextField { .key = "status", .value = std::to_string(status) },
        };
        write(Channel::Context, contextEndSequence(*id, fields));
    }
}

void ShellIntegration::setUserVar(std::string_view name, std::string_view value) const
{
    write(Channel::UserVar, userVarSequence(name, value));
}

bool ShellIntegration::active() const noexcept
{
    return _interactive && _enabled && _tty.isTerminal();
}

bool ShellIntegration::sends(Channel channel) const noexcept
{
    return !_quirks.insideTmux || TmuxTreatments[std::to_underlying(channel)] != TmuxTreatment::Drop;
}

void ShellIntegration::write(Channel channel, std::string_view sequence) const
{
    if (!_tty.isTerminal())
        return;

    auto const treatment =
        _quirks.insideTmux ? TmuxTreatments[std::to_underlying(channel)] : TmuxTreatment::Plain;
    switch (treatment)
    {
        case TmuxTreatment::Plain: _tty.writeToStdout(sequence); break;
        case TmuxTreatment::Passthrough: _tty.writeToStdout(tmuxPassthrough(sequence)); break;
        case TmuxTreatment::Drop: break;
    }
}

void ShellIntegration::endShellContext()
{
    // An open command context is left to commandFinished, which ends it with the outcome.
    auto const shellId = std::exchange(_shellContextId, std::nullopt);
    if (active() && shellId)
        write(Channel::Context, contextEndSequence(*shellId, {}));
}

} // namespace endo
