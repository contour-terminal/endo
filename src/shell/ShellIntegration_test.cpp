// SPDX-License-Identifier: Apache-2.0
#include <shell/testing/InjectedShell.hpp>

#include <core/platform/SystemInfo.hpp>
#include <core/platform/testing/InMemoryFileSystem.hpp>
#include <core/platform/testing/TestProcessEnvironment.hpp>
#include <core/platform/testing/TestWorkingDirectory.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <array>
#include <cstdint>
#include <format>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "ContextSignalling.hpp"
#include "ShellIntegration.hpp"
#include "ShellIntegrationSequences.hpp"
#include "TTY.hpp"

using namespace endo;
using namespace endo::shellintegration;
using namespace std::string_view_literals;

namespace
{

/// A TTY that records everything written to stdout.
class RecordingTTY final: public TTY
{
  public:
    explicit RecordingTTY(bool terminal): _terminal { terminal } {}

    [[nodiscard]] core::platform::NativeHandle inputFd() const noexcept override
    {
        return core::platform::InvalidHandle;
    }

    [[nodiscard]] core::platform::NativeHandle outputFd() const noexcept override
    {
        return core::platform::InvalidHandle;
    }

    [[nodiscard]] bool isTerminal() const noexcept override { return _terminal; }

    [[nodiscard]] std::expected<TerminalSize, ShellError> getSize() const override { return TerminalSize {}; }

    void setRawMode() override {}

    void restoreMode() override {}

    void setEchoEnabled(bool /*enabled*/) override {}

    [[nodiscard]] std::optional<char> readCharWithTimeout(std::chrono::milliseconds /*timeout*/) override
    {
        return std::nullopt;
    }

    void writeToStdout(std::string_view str) const override { _output += str; }

    void writeToStderr(std::string_view /*str*/) const override {}

    [[nodiscard]] bool isStderrTerminal() const noexcept override { return _terminal; }

    void writeToStdin(std::string_view /*str*/) const override {}

    [[nodiscard]] std::string const& output() const noexcept { return _output; }

    void clear() { _output.clear(); }

  private:
    bool _terminal;
    mutable std::string _output;
};

using Entries = std::map<std::string, std::string, std::less<>>;

constexpr auto FirstId = "00000000000000010000000000000002"sv;
constexpr auto SecondId = "00000000000000030000000000000004"sv;
constexpr auto MachineId = "0123456789abcdef0123456789abcdef"sv;

/// A ShellIntegration over a recording TTY with a deterministic random source.
struct Fixture
{
    explicit Fixture(bool terminal = true, Entries entries = {}):
        tty { terminal },
        environment { std::move(entries) },
        integration { tty,
                      environment,
                      ShellIntegration::Identity { .user = "alice",
                                                   .hostName = "box",
                                                   .machineId = std::string(MachineId),
                                                   .pid = 42 },
                      [this] { return ++counter; } }
    {
        integration.setInteractive(true);
    }

    RecordingTTY tty;
    core::platform::testing::TestProcessEnvironment environment;
    std::uint64_t counter = 0;
    ShellIntegration integration;
};

std::string shellStart(std::string_view id, std::string_view cwd)
{
    auto const fields = std::array {
        ContextField { .key = "type", .value = "shell" },
        ContextField { .key = "user", .value = "alice" },
        ContextField { .key = "hostname", .value = "box" },
        ContextField { .key = "machineid", .value = std::string(MachineId) },
        ContextField { .key = "pid", .value = "42" },
        ContextField { .key = "comm", .value = "endo" },
        ContextField { .key = "cwd", .value = std::string(cwd) },
    };
    return contextStartSequence(id, fields);
}

std::string promptUserVars()
{
    return userVarSequence("WEZTERM_USER", "alice") + userVarSequence("WEZTERM_HOST", "box")
           + userVarSequence("WEZTERM_IN_TMUX", "0");
}

} // namespace

TEST_CASE("ShellIntegration.silent_when_not_a_terminal")
{
    auto f = Fixture { false };
    f.integration.beforePrompt("/home/alice");
    f.integration.afterPromptDrawn("/home/alice");
    f.integration.commandStarted("ls", "/home/alice");
    f.integration.commandFinished(CommandOutcome { .status = 0 });
    f.integration.workingDirectoryChanged("/");
    f.integration.setUserVar("FOO", "bar");
    f.integration.setEnabled(false);
    f.integration.sessionEnded(0);
    CHECK(f.integration.promptZoneSequence(PromptZone::Start).empty());
    CHECK(f.tty.output().empty());
}

TEST_CASE("ShellIntegration.silent_when_disabled")
{
    auto f = Fixture {};
    f.integration.setEnabled(false);
    CHECK_FALSE(f.integration.enabled());
    f.integration.beforePrompt("/home/alice");
    f.integration.afterPromptDrawn("/home/alice");
    f.integration.commandStarted("ls", "/home/alice");
    f.integration.commandFinished(CommandOutcome { .status = 0 });
    f.integration.workingDirectoryChanged("/");
    f.integration.sessionEnded(0);
    CHECK(f.tty.output().empty());

    f.integration.setUserVar("FOO", "bar");
    CHECK(f.tty.output() == userVarSequence("FOO", "bar"));
}

TEST_CASE("ShellIntegration.silent_when_not_interactive")
{
    auto f = Fixture {};
    f.integration.setInteractive(false);
    f.integration.beforePrompt("/home/alice");
    f.integration.afterPromptDrawn("/home/alice");
    f.integration.commandStarted("ls", "/home/alice");
    f.integration.commandFinished(CommandOutcome { .status = 0 });
    CHECK(f.tty.output().empty());
}

TEST_CASE("ShellIntegration.prompt_cycle_order")
{
    auto f = Fixture {};
    f.integration.beforePrompt("/home/alice");
    f.integration.afterPromptDrawn("/home/alice");
    f.integration.commandStarted("ls -l", "/home/alice");
    f.integration.commandFinished(CommandOutcome { .status = 2 });

    auto const commandFields = std::array {
        ContextField { .key = "type", .value = "command" },
        ContextField { .key = "cmdline", .value = "ls -l" },
        ContextField { .key = "cwd", .value = "/home/alice" },
    };
    auto const endFields = commandEndFields(CommandOutcome { .status = 2 });
    auto const expected = currentDirectorySequence("/home/alice", "box") + shellStart(FirstId, "/home/alice")
                          + promptUserVars() + commandStartSequence("ls -l")
                          + contextStartSequence(SecondId, commandFields)
                          + userVarSequence("WEZTERM_PROG", "ls -l") + contextEndSequence(SecondId, endFields)
                          + commandFinishedSequence(2);
    CHECK(f.tty.output() == expected);

    // The next prompt resets WEZTERM_PROG, and does not repeat it when it is already empty.
    f.tty.clear();
    f.integration.afterPromptDrawn("/srv");
    CHECK(f.tty.output()
          == shellStart(FirstId, "/srv") + promptUserVars() + userVarSequence("WEZTERM_PROG", ""));
    f.tty.clear();
    f.integration.afterPromptDrawn("/srv");
    CHECK(f.tty.output() == shellStart(FirstId, "/srv") + promptUserVars());
}

TEST_CASE("ShellIntegration.reannounce_full_fields")
{
    auto f = Fixture {};
    f.integration.afterPromptDrawn("/a");
    f.tty.clear();
    f.integration.afterPromptDrawn("/b");
    CHECK(f.tty.output().starts_with(shellStart(FirstId, "/b")));
}

TEST_CASE("ShellIntegration.prog_sent_only_when_changed")
{
    auto f = Fixture {};
    f.integration.commandStarted("make", "/");
    f.integration.commandFinished(CommandOutcome { .status = 0 });
    f.tty.clear();
    f.integration.commandStarted("make", "/");
    CHECK(f.tty.output().find("SetUserVar=WEZTERM_PROG") == std::string::npos);
}

TEST_CASE("ShellIntegration.tmux")
{
    auto f = Fixture { true, Entries { { "TMUX", "/run/tmux,1,0" } } };
    CHECK(f.integration.quirks().insideTmux);
    f.integration.beforePrompt("/home/alice");
    f.integration.afterPromptDrawn("/home/alice");
    f.integration.commandStarted("ls", "/home/alice");
    f.integration.commandFinished(CommandOutcome { .status = 0 });
    f.integration.sessionEnded(0);

    auto const& out = f.tty.output();
    CHECK(out.find("]3008;") == std::string::npos);
    CHECK(out.find(tmuxPassthrough(userVarSequence("WEZTERM_IN_TMUX", "1"))) != std::string::npos);
    CHECK(out.find(tmuxPassthrough(userVarSequence("WEZTERM_PROG", "ls"))) != std::string::npos);
    CHECK(out.starts_with(currentDirectorySequence("/home/alice", "box")));
    CHECK(out.find(commandStartSequence("ls")) != std::string::npos);
    CHECK(out.ends_with(commandFinishedSequence(0)));

    f.tty.clear();
    f.integration.setUserVar("FOO", "bar");
    CHECK(f.tty.output() == tmuxPassthrough(userVarSequence("FOO", "bar")));
}

TEST_CASE("ShellIntegration.toggle_off_ends_context")
{
    auto f = Fixture {};
    f.integration.afterPromptDrawn("/");
    f.tty.clear();

    f.integration.setEnabled(false);
    CHECK(f.tty.output() == contextEndSequence(FirstId, {}));
    f.tty.clear();
    f.integration.setEnabled(false);
    CHECK(f.tty.output().empty());

    f.integration.setEnabled(true);
    CHECK(f.tty.output().empty());
    f.integration.afterPromptDrawn("/");
    CHECK(f.tty.output().starts_with(shellStart(SecondId, "/")));
}

TEST_CASE("ShellIntegration.toggle_off_mid_command_still_finishes_it")
{
    // `shell_integration <- false` typed at the prompt is itself a command: its 133;C was sent,
    // so its 133;D (and the command context's end=) must follow although the integration is off.
    auto f = Fixture {};
    f.integration.afterPromptDrawn("/");
    f.integration.commandStarted("shell_integration <- false", "/");
    f.tty.clear();

    f.integration.setEnabled(false);
    CHECK(f.tty.output() == contextEndSequence(FirstId, {}));
    f.tty.clear();

    f.integration.commandFinished(CommandOutcome { .status = 0 });
    CHECK(f.tty.output()
          == contextEndSequence(SecondId, commandEndFields(CommandOutcome { .status = 0 }))
                 + commandFinishedSequence(0));

    // The next command runs with the integration off: neither C nor D.
    f.tty.clear();
    f.integration.commandStarted("ls", "/");
    f.integration.commandFinished(CommandOutcome { .status = 0 });
    CHECK(f.tty.output().empty());
}

TEST_CASE("ShellIntegration.toggle_on_mid_command_sends_no_finish")
{
    // `shell_integration <- true` runs while the integration is off: no 133;C was sent for it,
    // so no 133;D may follow.
    auto f = Fixture {};
    f.integration.setEnabled(false);
    f.integration.commandStarted("shell_integration <- true", "/");
    f.integration.setEnabled(true);
    f.integration.commandFinished(CommandOutcome { .status = 0 });
    CHECK(f.tty.output().empty());

    // The next command is complete again.
    f.integration.commandStarted("ls", "/");
    f.integration.commandFinished(CommandOutcome { .status = 0 });
    CHECK(f.tty.output().starts_with(commandStartSequence("ls")));
    CHECK(f.tty.output().ends_with(commandFinishedSequence(0)));
}

TEST_CASE("ShellIntegration.session_end")
{
    auto f = Fixture {};
    f.integration.afterPromptDrawn("/");
    f.tty.clear();
    f.integration.sessionEnded(3);
    auto const fields = std::array {
        ContextField { .key = "exit", .value = "success" },
        ContextField { .key = "status", .value = "3" },
    };
    CHECK(f.tty.output() == contextEndSequence(FirstId, fields));
    f.tty.clear();
    f.integration.sessionEnded(3);
    CHECK(f.tty.output().empty());
}

TEST_CASE("ShellIntegration.working_directory_changed")
{
    auto f = Fixture {};
    f.integration.workingDirectoryChanged("/usr");
    CHECK(f.tty.output() == currentDirectorySequence("/usr", "box"));
}

TEST_CASE("ShellIntegration.zone_sequence_off")
{
    auto f = Fixture {};
    CHECK(f.integration.promptZoneSequence(PromptZone::Start) == promptZoneSequence(PromptZone::Start, {}));
    f.integration.setEnabled(false);
    CHECK(f.integration.promptZoneSequence(PromptZone::Start).empty());
}

TEST_CASE("ShellIntegration.no_machine_id_omits_field")
{
    auto tty = RecordingTTY { true };
    auto const environment = core::platform::testing::TestProcessEnvironment {};
    auto counter = std::uint64_t { 0 };
    auto integration = ShellIntegration {
        tty,
        environment,
        ShellIntegration::Identity { .user = "u", .hostName = "h", .machineId = std::nullopt, .pid = 7 },
        [&counter] { return ++counter; }
    };
    integration.setInteractive(true);
    integration.afterPromptDrawn("/");
    CHECK(tty.output().find("machineid=") == std::string::npos);
    CHECK(tty.output().find(";pid=7;comm=endo;cwd=/") != std::string::npos);
}

TEST_CASE("ShellIntegration.oversized_command_line_is_not_repeated")
{
    // A command line over 16 KiB is not percent-encoded into 133;C nor base64-encoded into
    // WEZTERM_PROG; OSC 3008's cmdline= carries it truncated.
    auto f = Fixture {};
    auto const huge = std::string(16385, 'x');
    f.integration.commandStarted(huge, "/");
    auto const& out = f.tty.output();
    CHECK(out.starts_with(commandStartSequence("")));
    CHECK(out.find("cmdline_url=") == std::string::npos);
    CHECK(out.find("WEZTERM_PROG") == std::string::npos);
    CHECK(out.find(";cmdline=xxx") != std::string::npos);
    f.integration.commandFinished(CommandOutcome { .status = 0 });
    CHECK(out.ends_with(commandFinishedSequence(0)));

    // Exactly 16 KiB is still sent whole.
    f.tty.clear();
    auto const limit = std::string(16384, 'x');
    f.integration.commandStarted(limit, "/");
    CHECK(f.tty.output().starts_with(commandStartSequence(limit)));
    CHECK(f.tty.output().find(userVarSequence("WEZTERM_PROG", limit)) != std::string::npos);
}

TEST_CASE("ShellIntegration.empty_user_omits_field")
{
    auto tty = RecordingTTY { true };
    auto const environment = core::platform::testing::TestProcessEnvironment {};
    auto counter = std::uint64_t { 0 };
    auto integration = ShellIntegration {
        tty,
        environment,
        ShellIntegration::Identity { .user = "", .hostName = "h", .machineId = std::nullopt, .pid = 7 },
        [&counter] { return ++counter; }
    };
    integration.setInteractive(true);
    integration.afterPromptDrawn("/");
    CHECK(tty.output().find("user=") == std::string::npos);
    CHECK(tty.output().find(";type=shell;hostname=h;pid=7;") != std::string::npos);
}

TEST_CASE("ShellIntegration.real_shell_is_interactive_by_default")
{
    // The shell's own interactive flag (true by default) must reach the emitter without anyone
    // calling setInteractive(true): otherwise the integration is silent in the real shell.
    auto tty = RecordingTTY { true };
    auto fs = core::platform::testing::InMemoryFileSystem {};
    auto env = core::platform::testing::TestProcessEnvironment {};
    auto workingDirectory = core::platform::testing::TestWorkingDirectory { "/test" };
    auto shell = Shell { tty, env, workingDirectory, fs };
    endo::testing::seedInjectedShell(fs, env, shell);
    fs.addDirectory("/test/sub");
    tty.clear();

    shell.execute("cd /test/sub");

    CHECK(tty.output().find(currentDirectorySequence("/test/sub", core::platform::cachedHostName()))
          != std::string::npos);
}

TEST_CASE("ShellIntegration.set_user_var_builtin_writes_to_the_terminal")
{
    // Checked over an injected terminal: the endo-test PTY is not a terminal on every platform,
    // and set-user-var is silent without one.
    auto const name = GENERATE("FOO"sv, "my_app.tab-1"sv);
    auto tty = RecordingTTY { true };
    auto fs = core::platform::testing::InMemoryFileSystem {};
    auto env = core::platform::testing::TestProcessEnvironment {};
    auto workingDirectory = core::platform::testing::TestWorkingDirectory { "/test" };
    auto shell = Shell { tty, env, workingDirectory, fs };
    endo::testing::seedInjectedShell(fs, env, shell);
    tty.clear();

    CHECK(shell.execute(std::format("set-user-var {} bar", name)) == 0);

    CHECK(tty.output() == std::format("\033]1337;SetUserVar={}=YmFy\033\\", name));
}
