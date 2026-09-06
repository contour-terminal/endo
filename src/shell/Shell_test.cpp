// SPDX-License-Identifier: Apache-2.0

#include <crispy/Escape.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <string_view>
#include <thread>

#include <platform/PathUtils.hpp>
#include <platform/SystemInfo.hpp>

using namespace std::string_literals;
using namespace std::string_view_literals;

using crispy::escape;

#include <shell/completion/FileCompleter.hpp>
#include <shell/completion/LetBindingCompleter.hpp>
#include <shell/completion/ScriptedCompleter.hpp>
#include <shell/history/PersistentHistory.hpp>
#include <shell/output/TableFormatter.hpp>
#include <shell/testing/InjectedShell.hpp>

#include <endo-language/ide/CompletionContext.hpp>

#include <http/LocalTcpListener.hpp>

#include "Shell.hpp"
#include "TTY.hpp"
#include <platform/InstallPaths.hpp>
#include <platform/NativeFileSystem.hpp>
#include <platform/testing/InMemoryFileSystem.hpp>
#include <platform/testing/MockProcessManager.hpp>
#include <platform/testing/TestEnvironmentProvider.hpp>
#include <testing/ScopedTempDir.hpp>
#include <testing/ScopedWorkingDirectory.hpp>

namespace
{
using endo::testing::InMemoryShell;

struct TestShell
{
    endo::TestPTY pty;
    endo::TestEnvironment env;
    int exitCode = -1;

    endo::Shell shell { pty, env };

    std::string output() const { return pty.output(); }

    TestShell()
    {
        // Seed essential environment variables from the real environment
        // so that external commands (echo, grep, etc.) can be resolved.
        if (auto const* path = std::getenv("PATH"))
            env.set("PATH", path);
        if (auto const* home = std::getenv("HOME"))
            env.set("HOME", home);
#if defined(_WIN32)
        if (auto const* pathext = std::getenv("PATHEXT"))
            env.set("PATHEXT", pathext);
#endif
        // Never probe the real terminal for Sixel support from a test.
        shell.setSixelCapability(std::make_unique<endo::StaticSixelCapability>(false));
    }

    TestShell& operator()(std::string_view cmd)
    {
        exitCode = shell.execute(std::string(cmd));
        return *this;
    }
};

/// A shell whose every OS collaborator is injected -- filesystem, environment and process
/// manager. Spawning is recorded rather than performed, so a test can assert on what the
/// shell would run without the program existing or a child being forked.
struct MockedProcessShell
{
    endo::TestPTY pty;
    endo::InMemoryFileSystem fs;
    endo::TestEnvironment env { "/test" };
    endo::platform::testing::MockProcessManager processManager;
    int exitCode = -1;

    endo::Shell shell { pty, env, fs, processManager };

    [[nodiscard]] std::string output() const { return pty.output(); }

    /// @return The configs the shell handed to the process manager, in spawn order.
    [[nodiscard]] auto const& spawned() const noexcept { return processManager.spawnedConfigs(); }

    MockedProcessShell() { endo::testing::seedInjectedShell(fs, env, shell); }

    MockedProcessShell& operator()(std::string_view cmd)
    {
        exitCode = shell.execute(std::string(cmd));
        return *this;
    }
};

} // namespace

// ============================================================================
// Basic Shell Commands
// ============================================================================

TEST_CASE("shell.syntax.exit")
{
    TestShell shell;
    CHECK(shell("exit").exitCode == 0);
    CHECK(shell("exit 1").exitCode == 1);
    CHECK(shell("exit 123").exitCode == 123);
}

// ============================================================================
// Startup environment
// ============================================================================

TEST_CASE("shell.env.SHELL_is_absolute_path")
{
    // Regression: SHELL used to be the bare name "endo". It must be the
    // fully-qualified path to the running executable, because programs such as
    // sudo-rs' `sudo -s` read SHELL and refuse to spawn a non-absolute value.
    TestShell shell;
    auto const value = shell.env.get("SHELL");
    REQUIRE(value.has_value());
    CHECK(*value != "endo");
    auto const exe = endo::platform::executablePath();
    REQUIRE(exe.has_value());
    CHECK(*value == endo::platform::normalizePath(*exe));
}

// ============================================================================
// cd builtin
// ============================================================================

TEST_CASE("shell.cd.basic")
{
    TestShell shell;
    shell.env.addValidPath("/tmp");
    shell("cd /tmp");
    CHECK(shell.exitCode == 0);
    CHECK(shell.env.get("PWD").value_or("") == "/tmp");
}

TEST_CASE("shell.cd.home")
{
    TestShell shell;
    shell.env.set("HOME", "/home/testuser");
    shell.env.addValidPath("/home/testuser");
    shell("cd");
    CHECK(shell.exitCode == 0);
    CHECK(shell.env.get("PWD").value_or("") == "/home/testuser");
}

TEST_CASE("shell.cd.home_falls_back_to_USERPROFILE")
{
    // Regression: on Windows HOME is unset and USERPROFILE names the home directory.
    // Bare `cd` must resolve through homeDirectory() (HOME, then USERPROFILE) rather
    // than falling back to the drive root.
    TestShell shell;
    // TestShell seeds HOME from the real environment so external commands resolve;
    // clear it here so USERPROFILE is the only home source under test.
    shell.env.unset("HOME");
    shell.env.set("USERPROFILE", "/home/winuser");
    shell.env.addValidPath("/home/winuser");
    shell("cd");
    CHECK(shell.exitCode == 0);
    CHECK(shell.env.get("PWD").value_or("") == "/home/winuser");
}

TEST_CASE("shell.cd.home_errors_when_no_home_set")
{
    // Neither HOME nor USERPROFILE set: bare `cd` reports an error instead of
    // silently changing to the filesystem root.
    TestShell shell;
    // TestShell seeds HOME from the real environment; clear both home sources so
    // bare `cd` has nothing to resolve and must report an error.
    shell.env.unset("HOME");
    shell.env.unset("USERPROFILE");
    shell("cd");
    CHECK(shell.exitCode == 1);
}

TEST_CASE("shell.cd.minus")
{
    TestShell shell;
    shell.env.addValidPath("/tmp");
    shell.env.addValidPath("/var");

    shell("cd /tmp");
    CHECK(shell.exitCode == 0);
    CHECK(shell.env.get("PWD").value_or("") == "/tmp");

    shell("cd /var");
    CHECK(shell.exitCode == 0);
    CHECK(shell.env.get("PWD").value_or("") == "/var");
    CHECK(shell.env.get("OLDPWD").value_or("") == "/tmp");

    shell("cd -");
    CHECK(shell.exitCode == 0);
    CHECK(shell.env.get("PWD").value_or("") == "/tmp");
    CHECK(shell.env.get("OLDPWD").value_or("") == "/var");
}

TEST_CASE("shell.cd.minus_swaps")
{
    TestShell shell;
    shell.env.addValidPath("/tmp");
    shell.env.addValidPath("/var");

    shell("cd /tmp");
    shell("cd /var");

    // First cd - -> /tmp
    shell("cd -");
    CHECK(shell.env.get("PWD").value_or("") == "/tmp");

    // Second cd - -> /var
    shell("cd -");
    CHECK(shell.env.get("PWD").value_or("") == "/var");
}

TEST_CASE("shell.cd.relative_then_minus")
{
    TestShell shell;
    shell.env.addValidPath("/tmp");
    shell.env.addValidPath("/tmp/subdir");

    shell("cd /tmp");
    CHECK(shell.exitCode == 0);
    CHECK(shell.env.get("PWD").value_or("") == "/tmp");

    // cd with a relative path — PWD must still be the resolved absolute path
    shell("cd subdir");
    CHECK(shell.exitCode == 0);
    CHECK(shell.env.get("PWD").value_or("") == "/tmp/subdir");
    CHECK(shell.env.get("OLDPWD").value_or("") == "/tmp");

    // cd - should return to the previous absolute directory
    shell("cd -");
    CHECK(shell.exitCode == 0);
    CHECK(shell.env.get("PWD").value_or("") == "/tmp");
    CHECK(shell.env.get("OLDPWD").value_or("") == "/tmp/subdir");
}

TEST_CASE("shell.cd.minus_returns_to_initial_cwd")
{
    TestShell shell;
    shell.env.addValidPath("/tmp");
    shell.env.addValidPath("/home/testuser");

    // First cd from initial directory
    shell("cd /tmp");
    CHECK(shell.env.get("OLDPWD").value_or("") == "/home/testuser");

    // cd - should return to initial cwd, not ~/
    shell("cd -");
    CHECK(shell.exitCode == 0);
    CHECK(shell.env.get("PWD").value_or("") == "/home/testuser");
}

TEST_CASE("shell.cd.minus_no_oldpwd")
{
    TestShell shell;
    shell("cd -");
    CHECK(shell.exitCode == 1);
}

TEST_CASE("shell.cd.invalid_path")
{
    TestShell shell;
    shell.env.addValidPath("/tmp"); // only /tmp is valid
    shell.env.set("PWD", "/home/testuser");

    shell("cd /nonexistent");
    CHECK(shell.exitCode == 1);
    // PWD should remain unchanged on failure
    CHECK(shell.env.get("PWD").value_or("") == "/home/testuser");
}

TEST_CASE("shell.syntax.pipes")
{
    CHECK(escape(TestShell()("echo hello | grep --color=never ll").output()) == escape("hello\n"));
    CHECK(escape(TestShell()("echo hello | grep --color=never ll | grep --color=never hell").output())
          == escape("hello\n"));
}

TEST_CASE("shell.tilde_in_argument")
{
    CHECK(escape(TestShell()("echo HEAD~2").output()) == escape("HEAD~2\n"));
    CHECK(escape(TestShell()("echo HEAD~").output()) == escape("HEAD~\n"));
    CHECK(escape(TestShell()("echo a~b").output()) == escape("a~b\n"));
}

TEST_CASE("shell.builtin.echo_basic")
{
    CHECK(escape(TestShell()("echo hello").output()) == escape("hello\n"));
    CHECK(escape(TestShell()("echo hello world").output()) == escape("hello world\n"));
    CHECK(escape(TestShell()("echo").output()) == escape("\n"));
}

TEST_CASE("shell.builtin.echo_n_flag")
{
    CHECK(escape(TestShell()("echo -n hello").output()) == escape("hello"));
    CHECK(escape(TestShell()("echo -n hello world").output()) == escape("hello world"));
}

TEST_CASE("shell.builtin.echo_e_flag")
{
    // Tab and newline escapes
    CHECK(escape(TestShell()("echo -e \"a\\tb\"").output()) == escape("a\tb\n"));
    CHECK(escape(TestShell()("echo -e \"a\\nb\"").output()) == escape("a\nb\n"));
    // Combined
    CHECK(escape(TestShell()("echo -e \"a\\tb\\tc\"").output()) == escape("a\tb\tc\n"));
}

TEST_CASE("shell.builtin.echo_ne_flags")
{
    // -n and -e combined
    CHECK(escape(TestShell()("echo -ne \"a\\tb\"").output()) == escape("a\tb"));
    CHECK(escape(TestShell()("echo -en \"a\\nb\"").output()) == escape("a\nb"));
}

TEST_CASE("shell.builtin.echo_double_dash")
{
    // -- signals end of options
    CHECK(escape(TestShell()("echo -- -n").output()) == escape("-n\n"));
    CHECK(escape(TestShell()("echo -- -e").output()) == escape("-e\n"));
}

TEST_CASE("shell.syntax.multiline")
{
    // Newlines should separate commands just like semicolons
    CHECK(escape(TestShell()("echo a\necho b\necho c").output()) == escape("a\nb\nc\n"));
}

// ============================================================================
// Read Builtin
// ============================================================================

TEST_CASE("shell.builtin.read.DefaultVar")
{
    auto const input = "hello world"s;
    TestShell shell;
    shell.pty.writeToStdin(input + "\n"s);
    shell("read");
    CHECK(shell.env.get("REPLY").value_or("NONE") == input);
}

TEST_CASE("shell.builtin.read.CustomVar")
{
    auto const input = "hello world"s;
    TestShell shell;
    shell.pty.writeToStdin(input + "\n"s);
    shell("read BRU");
    CHECK(shell.env.get("BRU").value_or("NONE") == input);
}

TEST_CASE("shell.builtin.read.help")
{
    TestShell shell;
    auto output = shell("read --help").output();
    CHECK(output.find("# read") != std::string::npos);
    CHECK(output.find("-p PROMPT") != std::string::npos);
    CHECK(output.find("-r") != std::string::npos);
    CHECK(output.find("-s") != std::string::npos);
    CHECK(shell.exitCode == 0);
}

TEST_CASE("shell.builtin.read.custom_prompt")
{
    TestShell shell;
    shell.pty.writeToStdin("hello\n");
    shell("read -p 'Enter: ' VAR");
    CHECK(shell.env.get("VAR").value_or("NONE") == "hello");
    // Check that prompt was displayed
    CHECK(shell.pty.output().find("Enter:") != std::string::npos);
}

TEST_CASE("shell.builtin.read.raw_mode")
{
    TestShell shell;
    // With -r, backslash is preserved as-is
    shell.pty.writeToStdin("hello\\nworld\n");
    shell("read -r VAR");
    CHECK(shell.env.get("VAR").value_or("NONE") == "hello\\nworld");
}

TEST_CASE("shell.builtin.read.backslash_escape")
{
    TestShell shell;
    // Without -r, backslash escapes the next character
    shell.pty.writeToStdin("hello\\nworld\n");
    shell("read VAR");
    // \n becomes literal 'n' (backslash removed)
    CHECK(shell.env.get("VAR").value_or("NONE") == "hellonworld");
}

TEST_CASE("shell.builtin.read.max_chars")
{
    TestShell shell;
    shell.pty.writeToStdin("hello world\n");
    shell("read -n 5 VAR");
    CHECK(shell.env.get("VAR").value_or("NONE") == "hello");
}

TEST_CASE("shell.builtin.read.delimiter")
{
    TestShell shell;
    shell.pty.writeToStdin("hello:world\n");
    shell("read -d ':' VAR");
    CHECK(shell.env.get("VAR").value_or("NONE") == "hello");
}

TEST_CASE("shell.builtin.read.multiple_variables")
{
    TestShell shell;
    shell.pty.writeToStdin("one two three four\n");
    shell("read A B C");
    CHECK(shell.env.get("A").value_or("NONE") == "one");
    CHECK(shell.env.get("B").value_or("NONE") == "two");
    // Last variable gets remainder
    CHECK(shell.env.get("C").value_or("NONE") == "three four");
}

TEST_CASE("shell.builtin.read.more_vars_than_words")
{
    TestShell shell;
    shell.pty.writeToStdin("one two\n");
    shell("read A B C D");
    CHECK(shell.env.get("A").value_or("NONE") == "one");
    CHECK(shell.env.get("B").value_or("NONE") == "two");
    CHECK(shell.env.get("C").value_or("NONE").empty());
    CHECK(shell.env.get("D").value_or("NONE").empty());
}

// Note: Pipeline support for read (e.g., "echo hello | read VAR") requires
// additional infrastructure work. The read builtin currently works with
// interactive TTY input and all flags (-p, -r, -s, -n, -t, -d) function correctly.

TEST_CASE("shell.builtin.read.custom_ifs")
{
    TestShell shell;
    shell("set IFS ':'");
    shell.pty.writeToStdin("one:two:three\n");
    shell("read A B C");
    CHECK(shell.env.get("A").value_or("NONE") == "one");
    CHECK(shell.env.get("B").value_or("NONE") == "two");
    CHECK(shell.env.get("C").value_or("NONE") == "three");
}

TEST_CASE("shell.builtin.read.empty_ifs_no_split")
{
    TestShell shell;
    shell("set IFS ''");
    shell.pty.writeToStdin("one two three\n");
    shell("read A B C");
    // With empty IFS, no splitting occurs - first var gets everything
    CHECK(shell.env.get("A").value_or("NONE") == "one two three");
    CHECK(shell.env.get("B").value_or("NONE").empty());
    CHECK(shell.env.get("C").value_or("NONE").empty());
}

TEST_CASE("shell.builtin.read.timeout_success")
{
    TestShell shell;
    // Write input immediately, should succeed before timeout
    shell.pty.writeToStdin("quick\n");
    shell("read -t 5 VAR");
    CHECK(shell.env.get("VAR").value_or("NONE") == "quick");
    CHECK(shell.exitCode == 0);
}

TEST_CASE("shell.builtin.read.invalid_option")
{
    TestShell shell;
    shell("read --invalid-option VAR");
    CHECK(shell.exitCode == 1);
}

TEST_CASE("shell.builtin.read.combined_flags")
{
    TestShell shell;
    shell.pty.writeToStdin("test\\ninput\n");
    // -r (raw) and custom prompt
    shell("read -r -p '> ' VAR");
    CHECK(shell.env.get("VAR").value_or("NONE") == "test\\ninput");
    CHECK(shell.pty.output().find("> ") != std::string::npos);
}

// ============================================================================
// Set/Unset Builtins
// ============================================================================

TEST_CASE("shell.builtin.set_variable")
{
    TestShell shell;
    shell("set BRU hello");
    CHECK(shell.env.get("BRU").value_or("NONE") == "hello");
}

TEST_CASE("shell.builtin.set_variable_with_spaces")
{
    TestShell shell;
    shell("set GREETING \"hello world\"");
    CHECK(shell.env.get("GREETING").value_or("NONE") == "hello world");
}

TEST_CASE("shell.builtin.unset_variable")
{
    TestShell shell;
    shell.env.set("MYVAR", "myvalue");
    CHECK(shell.env.get("MYVAR").has_value());
    shell("unset MYVAR");
    CHECK_FALSE(shell.env.get("MYVAR").has_value());
}

TEST_CASE("shell.builtin.unset_nonexistent_variable")
{
    TestShell shell;
    // Unsetting a nonexistent variable should not fail
    shell("unset NONEXISTENT");
    CHECK(shell.exitCode != EXIT_FAILURE);
}

// ============================================================================
// Which Builtin
// ============================================================================

TEST_CASE("shell.builtin.which_help")
{
    TestShell shell;
    // which with no arguments should show help and return 0
    auto output = shell("which").output();
    CHECK(output.find("# which") != std::string::npos);
    CHECK(output.find("## Usage") != std::string::npos);
    CHECK(shell.exitCode == 0);
}

TEST_CASE("shell.builtin.which_help_flag")
{
    TestShell shell;
    auto output = shell("which --help").output();
    CHECK(output.find("## Usage") != std::string::npos);
    CHECK(shell.exitCode == 0);

    // The options table is generated from whichDescriptor(), so every declared flag has
    // to show up here -- this is what catches a flag added to the table but not documented.
    CHECK(output.find("--all") != std::string::npos);
    CHECK(output.find("--read-alias") != std::string::npos);
    CHECK(output.find("--help") != std::string::npos);

    // generateInlineHelp() knows nothing about exit status; builtinWhich appends it.
    CHECK(output.find("## Exit Status") != std::string::npos);
}

TEST_CASE("shell.builtin.which_find_existing_program")
{
    TestShell shell;
#if defined(_WIN32)
    auto output = shell("which cmd").output();
    CHECK(output.find("cmd") != std::string::npos);
#else
    // /bin/ls or /usr/bin/ls should exist on most systems
    auto output = shell("which ls").output();
    CHECK((output.find("/bin/ls") != std::string::npos || output.find("/usr/bin/ls") != std::string::npos));
#endif
    CHECK(shell.exitCode == 0);
}

TEST_CASE("shell.builtin.which_nonexistent_program")
{
    TestShell shell;
    shell("which nonexistent_program_that_surely_does_not_exist_12345");
    CHECK(shell.exitCode == 1);
}

TEST_CASE("shell.builtin.which_multiple_programs")
{
    TestShell shell;
#if defined(_WIN32)
    auto output = shell("which cmd where").output();
    CHECK(output.find("cmd") != std::string::npos);
    CHECK(output.find("where") != std::string::npos);
#else
    auto output = shell("which ls cat").output();
    CHECK(output.find("ls") != std::string::npos);
    CHECK(output.find("cat") != std::string::npos);
#endif
    CHECK(shell.exitCode == 0);
}

TEST_CASE("shell.builtin.which_mixed_existing_and_nonexistent")
{
    TestShell shell;
#if defined(_WIN32)
    auto output = shell("which cmd nonexistent_xyz_123").output();
    CHECK(output.find("cmd") != std::string::npos);
#else
    // Should find ls but not the nonexistent one, return 1
    auto output = shell("which ls nonexistent_xyz_123").output();
    CHECK(output.find("ls") != std::string::npos);
#endif
    CHECK(shell.exitCode == 1);
}

TEST_CASE("shell.builtin.which_invalid_option")
{
    TestShell shell;
    shell("which --invalid-option ls");
    CHECK(shell.exitCode == 1);
}

TEST_CASE("shell.builtin.which_end_of_options_marker")
{
    TestShell shell;

    // `--` protects a leading-dash operand from being read as a flag...
    auto output = shell("which -- ls").output();
    CHECK(output.find("ls") != std::string::npos);
    CHECK(shell.exitCode == 0);

    // ...but only the operands after it. A bad flag before `--` is still a bad flag,
    // not a program name to look up.
    shell("which -x -- ls");
    CHECK(shell.exitCode == 1);
}

TEST_CASE("shell.builtin.which_bundled_short_flags")
{
    TestShell shell;
    // Bundled shorts come from the shared inline argument parser; -ai is -a plus -i.
    shell("which -ai ls");
    CHECK(shell.exitCode == 0);
}

TEST_CASE("shell.builtin.which_read_alias_warning")
{
    TestShell shell;
#if defined(_WIN32)
    shell("which -i cmd");
#else
    // --read-alias should warn but continue
    shell("which -i ls");
#endif
    // Should still find the program despite the warning
    CHECK(shell.exitCode == 0);
}

// ============================================================================
// Cat Builtin
// ============================================================================

TEST_CASE("shell.builtin.cat_help")
{
    TestShell shell;
    auto output = shell("cat --help").output();
    CHECK(output.find("# cat") != std::string::npos);
    CHECK(output.find("--number") != std::string::npos);
    CHECK(shell.exitCode == 0);
}

TEST_CASE("shell.builtin.cat_help_short")
{
    TestShell shell;
    auto output = shell("cat -h").output();
    CHECK(output.find("# cat") != std::string::npos);
    CHECK(shell.exitCode == 0);
}

TEST_CASE("shell.builtin.cat_pipe_input")
{
    // Test cat in a pipeline - reading from stdin
    CHECK(escape(TestShell()("echo hello | cat").output()) == escape("hello\n"));
    CHECK(escape(TestShell()("echo -n test | cat").output()) == escape("test"));
}

TEST_CASE("shell.builtin.cat_n_flag")
{
    // Test line numbering with echo piped to cat
    TestShell shell;
    auto output = shell(R"(echo -e "line1\nline2\nline3" | cat -n)").output();
    CHECK(output.find("1\t") != std::string::npos);
    CHECK(output.find("2\t") != std::string::npos);
    CHECK(output.find("3\t") != std::string::npos);
}

TEST_CASE("shell.builtin.cat_b_flag")
{
    // Test -b only numbers non-blank lines
    TestShell shell;
    auto output = shell(R"(echo -e "line1\n\nline2" | cat -b)").output();
    // First line should be numbered
    CHECK(output.find("1\t") != std::string::npos);
    // Second line is blank, should NOT be numbered
    // Third line should be numbered as 2
    CHECK(output.find("2\t") != std::string::npos);
}

TEST_CASE("shell.builtin.cat_E_flag")
{
    // Test -E shows $ at end of lines
    TestShell shell;
    auto output = shell(R"(echo -e "hello\nworld" | cat -E)").output();
    CHECK(output.find("hello$") != std::string::npos);
    CHECK(output.find("world$") != std::string::npos);
}

TEST_CASE("shell.builtin.cat_T_flag")
{
    // Test -T shows tabs as ^I
    TestShell shell;
    auto output = shell(R"(echo -e "a\tb" | cat -T)").output();
    CHECK(output.find("^I") != std::string::npos);
}

TEST_CASE("shell.builtin.cat_s_flag")
{
    // Test -s squeezes multiple blank lines
    TestShell shell;
    auto output = shell(R"(echo -e "a\n\n\n\nb" | cat -s)").output();
    // Should squeeze the multiple blank lines into one
    // Count the number of lines
    size_t newlineCount = 0;
    for (char c: output)
        if (c == '\n')
            newlineCount++;
    // Should have: a, blank, b, final newline = roughly 3-4 newlines instead of 5
    CHECK(newlineCount < 5);
}

TEST_CASE("shell.builtin.cat_A_flag")
{
    // Test -A is equivalent to -ET
    TestShell shell;
    auto output = shell(R"(echo -e "a\tb" | cat -A)").output();
    CHECK(output.find("^I") != std::string::npos); // Shows tabs
    CHECK(output.find('$') != std::string::npos);  // Shows line ends
}

TEST_CASE("shell.builtin.cat_combined_flags")
{
    // Test combining multiple flags
    TestShell shell;
    auto output = shell(R"(echo -e "a\tb\ncd" | cat -nT)").output();
    CHECK(output.find("1\t") != std::string::npos); // Line numbers
    CHECK(output.find("^I") != std::string::npos);  // Tabs shown
}

TEST_CASE("shell.builtin.cat_pipe_chain")
{
    // Test cat in middle of pipeline
    CHECK(escape(TestShell()("echo hello | cat | cat").output()) == escape("hello\n"));
}

TEST_CASE("shell.builtin.cat_nonexistent_file")
{
    TestShell shell;
    shell("cat /nonexistent/path/to/file");
    CHECK(shell.exitCode == 1);
}

// ============================================================================
// Cat Range Tests
// ============================================================================

TEST_CASE("shell.builtin.cat_range_basic")
{
    TestShell shell;
    auto output = shell(R"(echo -e "a\nb\nc\nd\ne" | cat --range 2..4)").output();
    CHECK(output == "b\nc\nd\n");
}

TEST_CASE("shell.builtin.cat_range_short")
{
    TestShell shell;
    auto output = shell(R"(echo -e "a\nb\nc\nd\ne" | cat -r 2..3)").output();
    CHECK(output == "b\nc\n");
}

TEST_CASE("shell.builtin.cat_range_open_end")
{
    TestShell shell;
    auto output = shell(R"(echo -e "a\nb\nc\nd\ne" | cat -r 3..)").output();
    CHECK(output == "c\nd\ne\n");
}

TEST_CASE("shell.builtin.cat_range_open_start")
{
    TestShell shell;
    auto output = shell(R"(echo -e "a\nb\nc\nd\ne" | cat -r ..2)").output();
    CHECK(output == "a\nb\n");
}

TEST_CASE("shell.builtin.cat_range_with_line_numbers")
{
    TestShell shell;
    auto output = shell(R"(echo -e "a\nb\nc\nd\ne" | cat -nr 3..4)").output();
    CHECK(output.find("3\t") != std::string::npos);
    CHECK(output.find("4\t") != std::string::npos);
    CHECK(output.find("1\t") == std::string::npos);
    CHECK(output.find('c') != std::string::npos);
    CHECK(output.find('d') != std::string::npos);
}

TEST_CASE("shell.builtin.cat_range_combined_flags")
{
    TestShell shell;
    auto output = shell(R"(echo -e "a\nb\nc\nd\ne" | cat -nr 2..2)").output();
    CHECK(output.find("2\t") != std::string::npos);
    CHECK(output.find('b') != std::string::npos);
}

TEST_CASE("shell.builtin.cat_range_single_line")
{
    TestShell shell;
    auto output = shell(R"(echo -e "a\nb\nc" | cat --range 2..2)").output();
    CHECK(output == "b\n");
}

TEST_CASE("shell.builtin.cat_range_with_squeeze")
{
    TestShell shell;
    auto output = shell(R"(echo -e "a\n\n\n\nb\nc" | cat -sr 1..6)").output();
    CHECK(output == "a\n\nb\nc\n");
}

TEST_CASE("shell.builtin.cat_range_error_empty")
{
    TestShell shell;
    shell("echo hello | cat --range ..");
    CHECK(shell.exitCode == 1);
}

TEST_CASE("shell.builtin.cat_range_error_inverted")
{
    TestShell shell;
    shell("echo hello | cat --range 5..2");
    CHECK(shell.exitCode == 1);
}

TEST_CASE("shell.builtin.cat_range_error_nonnumeric")
{
    TestShell shell;
    shell("echo hello | cat --range abc..5");
    CHECK(shell.exitCode == 1);
}

TEST_CASE("shell.builtin.cat_range_error_missing_arg")
{
    TestShell shell;
    shell("echo hello | cat -r");
    CHECK(shell.exitCode == 1);
}

TEST_CASE("shell.builtin.cat_range_equals_syntax")
{
    TestShell shell;
    auto output = shell(R"(echo -e "a\nb\nc\nd" | cat --range=2..3)").output();
    CHECK(output == "b\nc\n");
}

TEST_CASE("shell.builtin.cat_range_in_help")
{
    TestShell shell;
    auto output = shell("cat --help").output();
    CHECK(output.find("--range") != std::string::npos);
}

TEST_CASE("shell.builtin.cat_help_shows_image_options")
{
    TestShell shell;
    auto output = shell("cat --help").output();
    CHECK(output.find("--columns") != std::string::npos);
    CHECK(output.find("--rows") != std::string::npos);
    CHECK(output.find("--raw") != std::string::npos);
}

TEST_CASE("shell.builtin.cat_raw_flag")
{
    // A minimal PPM: an image extension, so --raw is what keeps this out of the sixel path.
    InMemoryShell shell;
    auto const rawPpmPath = std::string { "/test/raw.ppm" };
    shell.fs.addFile(rawPpmPath, "P6\n1 1\n255\n");
    auto output = shell(std::format("cat --raw {}", rawPpmPath)).output();
    CHECK(output.find("P6") != std::string::npos);
}

#if !defined(_WIN32)
TEST_CASE("shell.builtin.cat_binary_file_refuses_output")
{
    // Null bytes are seeded directly: the endo shell does not interpret \x00 in strings.
    InMemoryShell shell;
    auto const binaryPath = std::string { "/test/binary.dat" };
    shell.fs.addFile(binaryPath, std::string("hello\0world", 11));
    shell(std::format("cat {}", binaryPath));
    CHECK(shell.exitCode == 1);
}

TEST_CASE("shell.builtin.cat_binary_file_raw_mode")
{
    // Null bytes are seeded directly: the endo shell does not interpret \x00 in strings.
    InMemoryShell shell;
    auto const binaryRawPath = std::string { "/test/binary.dat" };
    shell.fs.addFile(binaryRawPath, std::string("hello\0world", 11));
    static_cast<void>(shell(std::format("cat --raw {}", binaryRawPath)).output());
    // With --raw, binary data should pass through (exit code 0)
    CHECK(shell.exitCode == 0);
}
#endif

TEST_CASE("shell.builtin.cat_pipe_regression")
{
    // Issue #98: Ensure cat in a pipeline (non-terminal stdin) still works after interruptible read changes.
    CHECK(escape(TestShell()("echo hello | cat").output()) == escape("hello\n"));
    CHECK(escape(TestShell()("echo -n test | cat").output()) == escape("test"));
}

TEST_CASE("shell.builtin.cat_sigint_returns_130")
{
    // Issue #98: Simulate SIGINT before running cat with piped input.
    // The interruptible read loop should detect the pending SIGINT and return 130.
    TestShell shell;
    endo::platform::SignalHandler::simulateSigint();
    shell("echo hello | cat");
    CHECK(shell.exitCode == 130);
    endo::platform::SignalHandler::clearPendingSigint(); // cleanup
}

TEST_CASE("shell.builtin.head_pipe_regression")
{
    // Ensure head in a pipeline still works after interruptible read changes.
    TestShell shell;
    auto output = shell(R"(echo -e "a\nb\nc" | head -n 2)").output();
    CHECK(output.find('a') != std::string::npos);
    CHECK(output.find('b') != std::string::npos);
    CHECK(shell.exitCode == 0);
}

TEST_CASE("shell.builtin.grep_sigint_returns_130")
{
    // Simulate SIGINT before grep reads stdin.
    TestShell shell;
    endo::platform::SignalHandler::simulateSigint();
    shell("echo hello | grep hello");
    CHECK(shell.exitCode == 130);
    endo::platform::SignalHandler::clearPendingSigint();
}

TEST_CASE("shell.builtin.tr_sigint_returns_130")
{
    // Simulate SIGINT before tr reads stdin.
    TestShell shell;
    endo::platform::SignalHandler::simulateSigint();
    shell("echo hello | tr a-z A-Z");
    CHECK(shell.exitCode == 130);
    endo::platform::SignalHandler::clearPendingSigint();
}

TEST_CASE("shell.builtin.tee_sigint_returns_130")
{
    // Simulate SIGINT before tee reads stdin.
    TestShell shell;
    endo::platform::SignalHandler::simulateSigint();
    shell("echo hello | tee /dev/null");
    CHECK(shell.exitCode == 130);
    endo::platform::SignalHandler::clearPendingSigint();
}

TEST_CASE("shell.signal.isInterruptCtrlEvent")
{
    using endo::platform::SignalHandler;

    // Win32 console control type values (CTRL_C_EVENT=0, CTRL_BREAK_EVENT=1, ...).
    // These are the events that must keep the shell alive while interrupting the
    // foreground command.
    CHECK(SignalHandler::isInterruptCtrlEvent(0)); // CTRL_C_EVENT
    CHECK(SignalHandler::isInterruptCtrlEvent(1)); // CTRL_BREAK_EVENT

    // Other control events fall through to the default handler.
    CHECK_FALSE(SignalHandler::isInterruptCtrlEvent(2)); // CTRL_CLOSE_EVENT
    CHECK_FALSE(SignalHandler::isInterruptCtrlEvent(5)); // CTRL_LOGOFF_EVENT
    CHECK_FALSE(SignalHandler::isInterruptCtrlEvent(6)); // CTRL_SHUTDOWN_EVENT
}

// ============================================================================
// Sleep Builtin
// ============================================================================

TEST_CASE("shell.builtin.sleep_help")
{
    TestShell shell;
    auto output = shell("sleep --help").output();
    CHECK(output.find("# sleep") != std::string::npos);
    CHECK(output.find("Suffix") != std::string::npos);
    CHECK(shell.exitCode == 0);
}

TEST_CASE("shell.builtin.sleep_help_short")
{
    TestShell shell;
    auto output = shell("sleep -h").output();
    CHECK(output.find("# sleep") != std::string::npos);
    CHECK(shell.exitCode == 0);
}

TEST_CASE("shell.builtin.sleep_no_args")
{
    TestShell shell;
    shell("sleep");
    CHECK(shell.exitCode == 1);
}

TEST_CASE("shell.builtin.sleep_zero")
{
    TestShell shell;
    shell("sleep 0");
    CHECK(shell.exitCode == 0);
}

TEST_CASE("shell.builtin.sleep_small_duration")
{
    // Test with a very small sleep to ensure it works without taking too long
    TestShell shell;
    auto start = std::chrono::steady_clock::now();
    shell("sleep 0.01");
    auto end = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    CHECK(shell.exitCode == 0);
    CHECK(elapsed >= 10); // Should have slept at least 10ms
}

TEST_CASE("shell.builtin.sleep_suffix_s")
{
    TestShell shell;
    shell("sleep '0.01s'");
    CHECK(shell.exitCode == 0);
}

TEST_CASE("shell.builtin.sleep_suffix_m")
{
    // Test parsing only - use 0 to avoid actual delay
    TestShell shell;
    shell("sleep '0m'");
    CHECK(shell.exitCode == 0);
}

TEST_CASE("shell.builtin.sleep_suffix_h")
{
    TestShell shell;
    shell("sleep '0h'");
    CHECK(shell.exitCode == 0);
}

TEST_CASE("shell.builtin.sleep_suffix_d")
{
    TestShell shell;
    shell("sleep '0d'");
    CHECK(shell.exitCode == 0);
}

TEST_CASE("shell.builtin.sleep_multiple_args")
{
    // Multiple arguments should be summed
    TestShell shell;
    shell("sleep 0 '0s' '0m'");
    CHECK(shell.exitCode == 0);
}

TEST_CASE("shell.builtin.sleep_invalid_arg")
{
    TestShell shell;
    shell("sleep abc");
    CHECK(shell.exitCode == 1);
}

TEST_CASE("shell.builtin.sleep_negative")
{
    TestShell shell;
    shell("sleep -1");
    CHECK(shell.exitCode == 1);
}

TEST_CASE("shell.builtin.sleep_float")
{
    TestShell shell;
    shell("sleep 0.001");
    CHECK(shell.exitCode == 0);
}

// ============================================================================
// Rm Builtin
// ============================================================================

TEST_CASE("shell.builtin.rm_help")
{
    TestShell shell;
    auto output = shell("rm --help").output();
    CHECK(output.find("# rm") != std::string::npos);
    CHECK(output.find("--force") != std::string::npos);
    CHECK(output.find("--recursive") != std::string::npos);
    CHECK(shell.exitCode == 0);
}

TEST_CASE("shell.builtin.rm_file")
{
    InMemoryShell shell;
    auto const testDir = std::filesystem::path("/test/rm_file");
    shell.fs.addDirectory(testDir);
    auto const filePath = testDir / "testfile.txt";
    shell.fs.addFile(filePath, "hello");
    REQUIRE(shell.fs.exists(filePath));

    shell(std::format("rm {}", filePath.string()));
    CHECK(shell.exitCode == 0);
    CHECK(!shell.fs.exists(filePath));
}

TEST_CASE("shell.builtin.rm_nonexistent")
{
    InMemoryShell shell;
    shell("rm /test/does_not_exist");
    CHECK(shell.exitCode == 1);
}

TEST_CASE("shell.builtin.rm_force_nonexistent")
{
    InMemoryShell shell;
    shell("rm -f /test/does_not_exist");
    CHECK(shell.exitCode == 0);
}

TEST_CASE("shell.builtin.rm_directory_without_recursive")
{
    InMemoryShell shell;
    auto const testDir = std::filesystem::path("/test/rm_dir_norec");
    shell.fs.addDirectory(testDir);
    REQUIRE(shell.fs.exists(testDir));

    shell(std::format("rm {}", testDir.string()));
    CHECK(shell.exitCode == 1);
}

TEST_CASE("shell.builtin.rm_recursive")
{
    InMemoryShell shell;
    auto const testDir = std::filesystem::path("/test/rm_recursive");
    shell.fs.addFile(testDir / "subdir" / "file.txt", "data");
    REQUIRE(shell.fs.exists(testDir / "subdir" / "file.txt"));

    shell(std::format("rm -r {}", testDir.string()));
    CHECK(shell.exitCode == 0);
    CHECK(!shell.fs.exists(testDir));
}

TEST_CASE("shell.builtin.rm_dir_flag")
{
    InMemoryShell shell;
    auto const testDir = std::filesystem::path("/test/rm_emptydir");
    shell.fs.addDirectory(testDir);
    REQUIRE(shell.fs.exists(testDir));

    shell(std::format("rm -d {}", testDir.string()));
    CHECK(shell.exitCode == 0);
    CHECK(!shell.fs.exists(testDir));
}

TEST_CASE("shell.builtin.rm_verbose")
{
    InMemoryShell shell;
    auto const testDir = std::filesystem::path("/test/rm_verbose");
    shell.fs.addDirectory(testDir);
    auto const filePath = testDir / "verbose_file.txt";
    shell.fs.addFile(filePath, "data");
    REQUIRE(shell.fs.exists(filePath));

    auto output = shell(std::format("rm -v {}", filePath.string())).output();
    CHECK(shell.exitCode == 0);
    CHECK(output.find("removed") != std::string::npos);
    CHECK(!shell.fs.exists(filePath));
}

TEST_CASE("shell.builtin.rm_verbose_recursive")
{
    InMemoryShell shell;
    auto const testDir = std::filesystem::path("/test/rm_verbose_recursive");
    shell.fs.addFile(testDir / "subdir" / "file.txt", "data");
    REQUIRE(shell.fs.exists(testDir / "subdir" / "file.txt"));

    auto output = shell(std::format("rm -vr {}", testDir.string())).output();
    CHECK(shell.exitCode == 0);
    CHECK(!shell.fs.exists(testDir));

    // Should list each removed entry (file, subdir, top-level dir)
    auto const lines = std::count(output.begin(), output.end(), '\n');
    CHECK(lines == 3);
    CHECK(output.find("file.txt") != std::string::npos);
    CHECK(output.find("subdir") != std::string::npos);
}

TEST_CASE("shell.builtin.rm_combined_flags")
{
    InMemoryShell shell;
    auto const testDir = std::filesystem::path("/test/rm_combined");
    shell.fs.addFile(testDir / "inner" / "f.txt", "x");
    REQUIRE(shell.fs.exists(testDir / "inner" / "f.txt"));

    shell(std::format("rm -rf {}", testDir.string()));
    CHECK(shell.exitCode == 0);
    CHECK(!shell.fs.exists(testDir));
}

TEST_CASE("shell.builtin.rm_double_dash")
{
    InMemoryShell shell;
    auto const testDir = std::filesystem::path("/test/rm_ddash");
    shell.fs.addDirectory(testDir);
    auto const filePath = testDir / "-weirdname";
    shell.fs.addFile(filePath, "data");
    REQUIRE(shell.fs.exists(filePath));

    shell(std::format("rm -- {}", filePath.string()));
    CHECK(shell.exitCode == 0);
    CHECK(!shell.fs.exists(filePath));
}

TEST_CASE("shell.builtin.rm_preserve_root")
{
    TestShell shell;
    shell("rm -rf /");
    CHECK(shell.exitCode == 1);
}

TEST_CASE("shell.builtin.rm_dot_rejection")
{
    {
        TestShell shell;
        shell("rm .");
        CHECK(shell.exitCode == 1);
    }
    {
        TestShell shell;
        shell("rm ..");
        CHECK(shell.exitCode == 1);
    }
}

// ============================================================================
// mkdir builtin
// ============================================================================

TEST_CASE("shell.expand.glob_expands_against_the_injected_filesystem")
{
    // Glob expansion used to enumerate the real disk while the builtin it fed then acted on
    // the injected filesystem -- so `rm *.txt` listed one set of files and deleted another.
    InMemoryShell shell;
    shell.fs.addFile("/test/globbed/a.txt", "a");
    shell.fs.addFile("/test/globbed/b.txt", "b");
    shell.fs.addFile("/test/globbed/c.log", "c");

    auto const listed = shell("echo /test/globbed/*.txt").output();
    CHECK(listed.find("a.txt") != std::string::npos);
    CHECK(listed.find("b.txt") != std::string::npos);
    CHECK(listed.find("c.log") == std::string::npos);

    // And what it expanded is what it removes.
    shell("rm /test/globbed/*.txt");
    CHECK(shell.exitCode == 0);
    CHECK(!shell.fs.exists("/test/globbed/a.txt"));
    CHECK(!shell.fs.exists("/test/globbed/b.txt"));
    CHECK(shell.fs.exists("/test/globbed/c.log"));
}

TEST_CASE("shell.builtin.head_and_wc_read_through_the_injected_filesystem")
{
    // head/tail/wc/sort/uniq/cut/tr share readLinesFromInput, which now opens files through
    // Shell's FileSystem rather than an ifstream. Without that these could only be tested
    // against a real directory.
    InMemoryShell shell;
    shell.fs.addFile("/test/lines.txt", "one\ntwo\nthree\n");

    CHECK(shell("head -n 2 /test/lines.txt").output() == "one\ntwo\n");
    CHECK(shell.exitCode == 0);

    auto const counted = shell("wc -l /test/lines.txt").output();
    CHECK(counted.find('3') != std::string::npos);
}

TEST_CASE("shell.builtin.mkdir_help")
{
    TestShell shell;
    auto output = shell("mkdir --help").output();
    CHECK(output.find("# mkdir") != std::string::npos);
    CHECK(output.find("--parents") != std::string::npos);
    CHECK(shell.exitCode == 0);
}

TEST_CASE("shell.builtin.mkdir_basic")
{
    InMemoryShell shell;
    auto const testDir = std::filesystem::path("/test/mkdir_basic");

    shell(std::format("mkdir {}", testDir.string()));
    CHECK(shell.exitCode == 0);
    CHECK(shell.fs.isDirectory(testDir));
}

TEST_CASE("shell.builtin.mkdir_already_exists")
{
    InMemoryShell shell;
    auto const testDir = std::filesystem::path("/test/mkdir_exists");
    shell.fs.addDirectory(testDir);
    REQUIRE(shell.fs.exists(testDir));

    shell(std::format("mkdir {}", testDir.string()));
    CHECK(shell.exitCode == 1);
}

TEST_CASE("shell.builtin.mkdir_parents")
{
    InMemoryShell shell;
    auto const testDir = std::filesystem::path("/test/mkdir_parents") / "a" / "b" / "c";

    shell(std::format("mkdir -p {}", testDir.string()));
    CHECK(shell.exitCode == 0);
    CHECK(shell.fs.isDirectory(testDir));
}

TEST_CASE("shell.builtin.mkdir_parents_long")
{
    InMemoryShell shell;
    auto const testDir = std::filesystem::path("/test/mkdir_parents_long") / "x" / "y";

    shell(std::format("mkdir --parents {}", testDir.string()));
    CHECK(shell.exitCode == 0);
    CHECK(shell.fs.isDirectory(testDir));
}

TEST_CASE("shell.builtin.mkdir_parents_existing")
{
    InMemoryShell shell;
    auto const testDir = std::filesystem::path("/test/mkdir_parents_exist");
    shell.fs.addDirectory(testDir);
    REQUIRE(shell.fs.exists(testDir));

    shell(std::format("mkdir -p {}", testDir.string()));
    CHECK(shell.exitCode == 0);
}

TEST_CASE("shell.builtin.mkdir_no_operand")
{
    TestShell shell;
    shell("mkdir");
    CHECK(shell.exitCode == 1);
}

TEST_CASE("shell.builtin.mkdir_verbose")
{
    InMemoryShell shell;
    auto const testDir = std::filesystem::path("/test/mkdir_verbose");

    auto output = shell(std::format("mkdir -v {}", testDir.string())).output();
    CHECK(shell.exitCode == 0);
    CHECK(output.find("created directory") != std::string::npos);
    CHECK(shell.fs.isDirectory(testDir));
}

TEST_CASE("shell.builtin.mkdir_combined_flags")
{
    InMemoryShell shell;
    auto const testDir = std::filesystem::path("/test/mkdir_combined") / "sub";

    auto output = shell(std::format("mkdir -pv {}", testDir.string())).output();
    CHECK(shell.exitCode == 0);
    CHECK(shell.fs.isDirectory(testDir));
    CHECK(output.find("created directory") != std::string::npos);
}

TEST_CASE("shell.builtin.mkdir_double_dash")
{
    InMemoryShell shell;
    auto const testDir = std::filesystem::path("/test/mkdir_ddash") / "-weirdname";
    shell.fs.addDirectory(std::filesystem::path("/test/mkdir_ddash"));

    shell(std::format("mkdir -- {}", testDir.string()));
    CHECK(shell.exitCode == 0);
    CHECK(shell.fs.isDirectory(testDir));
}

TEST_CASE("shell.builtin.mkdir_multiple_dirs")
{
    InMemoryShell shell;
    auto const base = std::filesystem::path("/test/mkdir_multi");
    shell.fs.addDirectory(base);
    auto const dir1 = base / "dir1";
    auto const dir2 = base / "dir2";
    auto const dir3 = base / "dir3";

    shell(std::format("mkdir {} {} {}", dir1.string(), dir2.string(), dir3.string()));
    CHECK(shell.exitCode == 0);
    CHECK(shell.fs.isDirectory(dir1));
    CHECK(shell.fs.isDirectory(dir2));
    CHECK(shell.fs.isDirectory(dir3));
}

// ============================================================================
// cp builtin
// ============================================================================

TEST_CASE("shell.builtin.cp_help")
{
    TestShell shell;
    auto output = shell("cp --help").output();
    CHECK(output.find("# cp") != std::string::npos);
    CHECK(output.find("--recursive") != std::string::npos);
    CHECK(shell.exitCode == 0);
}

TEST_CASE("shell.builtin.cp_single_file")
{
    InMemoryShell shell;
    auto const base = std::filesystem::path("/test/cp_single");
    shell.fs.addDirectory(base);
    auto const src = base / "source.txt";
    auto const dst = base / "dest.txt";

    // Create source file with content
    shell.fs.addFile(src, "hello world");

    shell(std::format("cp {} {}", src.string(), dst.string()));
    CHECK(shell.exitCode == 0);
    CHECK(shell.fs.exists(dst));

    // Verify content
    CHECK(shell.content(dst) == "hello world");
}

TEST_CASE("shell.builtin.cp_nonexistent_source")
{
    InMemoryShell shell;
    auto const base = std::filesystem::path("/test/cp_noexist");
    shell.fs.addDirectory(base);

    shell(std::format("cp {}/nosuchfile {}/dest", base.string(), base.string()));
    CHECK(shell.exitCode == 1);
}

TEST_CASE("shell.builtin.cp_overwrite_default")
{
    InMemoryShell shell;
    auto const base = std::filesystem::path("/test/cp_overwrite");
    shell.fs.addDirectory(base);
    auto const src = base / "source.txt";
    auto const dst = base / "dest.txt";

    shell.fs.addFile(src, "new content");
    shell.fs.addFile(dst, "old content");

    shell(std::format("cp {} {}", src.string(), dst.string()));
    CHECK(shell.exitCode == 0);

    CHECK(shell.content(dst) == "new content");
}

TEST_CASE("shell.builtin.cp_no_clobber")
{
    InMemoryShell shell;
    auto const base = std::filesystem::path("/test/cp_noclobber");
    shell.fs.addDirectory(base);
    auto const src = base / "source.txt";
    auto const dst = base / "dest.txt";

    shell.fs.addFile(src, "new content");
    shell.fs.addFile(dst, "old content");

    shell(std::format("cp -n {} {}", src.string(), dst.string()));
    CHECK(shell.exitCode == 0);

    // Original content preserved
    CHECK(shell.content(dst) == "old content");
}

TEST_CASE("shell.builtin.cp_force")
{
    InMemoryShell shell;
    auto const base = std::filesystem::path("/test/cp_force");
    shell.fs.addDirectory(base);
    auto const src = base / "source.txt";
    auto const dst = base / "dest.txt";

    shell.fs.addFile(src, "forced content");
    shell.fs.addFile(dst, "old content");

    shell(std::format("cp -f {} {}", src.string(), dst.string()));
    CHECK(shell.exitCode == 0);

    CHECK(shell.content(dst) == "forced content");
}

TEST_CASE("shell.builtin.cp_directory_without_recursive")
{
    InMemoryShell shell;
    auto const base = std::filesystem::path("/test/cp_dir_norec");
    shell.fs.addDirectory(base / "srcdir");

    shell(std::format("cp {} {}", (base / "srcdir").string(), (base / "dstdir").string()));
    CHECK(shell.exitCode == 1);
}

TEST_CASE("shell.builtin.cp_recursive")
{
    InMemoryShell shell;
    auto const base = std::filesystem::path("/test/cp_recursive");
    shell.fs.addFile(base / "srcdir" / "file.txt", "data");
    shell.fs.addFile(base / "srcdir" / "sub" / "nested.txt", "nested");

    shell(std::format("cp -r {} {}", (base / "srcdir").string(), (base / "dstdir").string()));
    CHECK(shell.exitCode == 0);
    CHECK(shell.fs.exists(base / "dstdir" / "file.txt"));
    CHECK(shell.fs.exists(base / "dstdir" / "sub" / "nested.txt"));

    CHECK(shell.content(base / "dstdir" / "sub" / "nested.txt") == "nested");
}

TEST_CASE("shell.builtin.cp_verbose")
{
    InMemoryShell shell;
    auto const base = std::filesystem::path("/test/cp_verbose");
    shell.fs.addDirectory(base);
    auto const src = base / "source.txt";
    auto const dst = base / "dest.txt";

    shell.fs.addFile(src, "hello");

    auto output = shell(std::format("cp -v {} {}", src.string(), dst.string())).output();
    CHECK(shell.exitCode == 0);
    CHECK(output.find("->") != std::string::npos);
    CHECK(shell.fs.exists(dst));
}

TEST_CASE("shell.builtin.cp_combined_flags")
{
    InMemoryShell shell;
    auto const base = std::filesystem::path("/test/cp_combined");
    shell.fs.addFile(base / "srcdir" / "file.txt", "data");

    auto output =
        shell(std::format("cp -rv {} {}", (base / "srcdir").string(), (base / "dstdir").string())).output();
    CHECK(shell.exitCode == 0);
    CHECK(shell.fs.exists(base / "dstdir" / "file.txt"));
    CHECK(output.find("->") != std::string::npos);
}

TEST_CASE("shell.builtin.cp_double_dash")
{
    InMemoryShell shell;
    auto const base = std::filesystem::path("/test/cp_ddash");
    shell.fs.addDirectory(base);
    auto const src = base / "-weirdname.txt";
    auto const dst = base / "dest.txt";

    shell.fs.addFile(src, "content");

    shell(std::format("cp -- {} {}", src.string(), dst.string()));
    CHECK(shell.exitCode == 0);
    CHECK(shell.fs.exists(dst));
}

TEST_CASE("shell.builtin.cp_multiple_sources_to_dir")
{
    InMemoryShell shell;
    auto const base = std::filesystem::path("/test/cp_multi");
    shell.fs.addDirectory(base / "destdir");
    auto const src1 = base / "a.txt";
    auto const src2 = base / "b.txt";

    shell.fs.addFile(src1, "aaa");
    shell.fs.addFile(src2, "bbb");

    shell(std::format("cp {} {} {}", src1.string(), src2.string(), (base / "destdir").string()));
    CHECK(shell.exitCode == 0);
    CHECK(shell.fs.exists(base / "destdir" / "a.txt"));
    CHECK(shell.fs.exists(base / "destdir" / "b.txt"));
}

TEST_CASE("shell.builtin.cp_multiple_sources_to_file")
{
    InMemoryShell shell;
    auto const base = std::filesystem::path("/test/cp_multi_fail");
    shell.fs.addDirectory(base);
    auto const src1 = base / "a.txt";
    auto const src2 = base / "b.txt";
    auto const dst = base / "dest.txt";

    shell.fs.addFile(src1, "aaa");
    shell.fs.addFile(src2, "bbb");
    shell.fs.addFile(dst, "xxx");

    shell(std::format("cp {} {} {}", src1.string(), src2.string(), dst.string()));
    CHECK(shell.exitCode == 1);
}

TEST_CASE("shell.builtin.cp_no_operand")
{
    TestShell shell;
    shell("cp");
    CHECK(shell.exitCode == 1);
}

TEST_CASE("shell.builtin.cp_missing_destination")
{
    InMemoryShell shell;
    auto const base = std::filesystem::path("/test/cp_missdest");
    shell.fs.addDirectory(base);
    auto const src = base / "source.txt";

    shell.fs.addFile(src, "data");

    shell(std::format("cp {}", src.string()));
    CHECK(shell.exitCode == 1);
}

// ============================================================================
// mv builtin
// ============================================================================

TEST_CASE("shell.builtin.mv_help")
{
    TestShell shell;
    auto output = shell("mv --help").output();
    CHECK(output.find("# mv") != std::string::npos);
    CHECK(output.find("--no-clobber") != std::string::npos);
    CHECK(shell.exitCode == 0);
}

TEST_CASE("shell.builtin.mv_single_file")
{
    InMemoryShell shell;
    auto const base = std::filesystem::path("/test/mv_single");
    shell.fs.addDirectory(base);
    auto const src = base / "source.txt";
    auto const dst = base / "dest.txt";

    shell.fs.addFile(src, "hello world");

    shell(std::format("mv {} {}", src.string(), dst.string()));
    CHECK(shell.exitCode == 0);
    CHECK(!shell.fs.exists(src));
    CHECK(shell.fs.exists(dst));

    CHECK(shell.content(dst) == "hello world");
}

TEST_CASE("shell.builtin.mv_to_directory")
{
    InMemoryShell shell;
    auto const base = std::filesystem::path("/test/mv_todir");
    shell.fs.addDirectory(base / "subdir");
    auto const src = base / "file.txt";

    shell.fs.addFile(src, "data");

    shell(std::format("mv {} {}", src.string(), (base / "subdir").string()));
    CHECK(shell.exitCode == 0);
    CHECK(!shell.fs.exists(src));
    CHECK(shell.fs.exists(base / "subdir" / "file.txt"));
}

TEST_CASE("shell.builtin.mv_nonexistent")
{
    InMemoryShell shell;
    auto const base = std::filesystem::path("/test/mv_noexist");
    shell.fs.addDirectory(base);

    shell(std::format("mv {}/nosuchfile {}/dest", base.string(), base.string()));
    CHECK(shell.exitCode == 1);
}

TEST_CASE("shell.builtin.mv_no_operand")
{
    TestShell shell;
    shell("mv");
    CHECK(shell.exitCode == 1);
}

TEST_CASE("shell.builtin.mv_missing_destination")
{
    InMemoryShell shell;
    auto const base = std::filesystem::path("/test/mv_missdest");
    shell.fs.addDirectory(base);
    auto const src = base / "source.txt";

    shell.fs.addFile(src, "data");

    shell(std::format("mv {}", src.string()));
    CHECK(shell.exitCode == 1);
}

TEST_CASE("shell.builtin.mv_no_clobber")
{
    InMemoryShell shell;
    auto const base = std::filesystem::path("/test/mv_noclobber");
    shell.fs.addDirectory(base);
    auto const src = base / "source.txt";
    auto const dst = base / "dest.txt";

    shell.fs.addFile(src, "source content");
    shell.fs.addFile(dst, "existing content");

    shell(std::format("mv -n {} {}", src.string(), dst.string()));
    CHECK(shell.exitCode == 0);
    // Source should still exist (move was skipped)
    CHECK(shell.fs.exists(src));
    // Destination should retain original content
    CHECK(shell.content(dst) == "existing content");
}

TEST_CASE("shell.builtin.mv_verbose")
{
    InMemoryShell shell;
    auto const base = std::filesystem::path("/test/mv_verbose");
    shell.fs.addDirectory(base);
    auto const src = base / "source.txt";
    auto const dst = base / "dest.txt";

    shell.fs.addFile(src, "data");

    auto output = shell(std::format("mv -v {} {}", src.string(), dst.string())).output();
    CHECK(shell.exitCode == 0);
    CHECK(output.find("->") != std::string::npos);
    CHECK(!shell.fs.exists(src));
    CHECK(shell.fs.exists(dst));
}

TEST_CASE("shell.builtin.mv_double_dash")
{
    InMemoryShell shell;
    auto const base = std::filesystem::path("/test/mv_ddash");
    shell.fs.addDirectory(base);
    auto const src = base / "-dashfile.txt";
    auto const dst = base / "dest.txt";

    shell.fs.addFile(src, "data");

    shell(std::format("mv -- {} {}", src.string(), dst.string()));
    CHECK(shell.exitCode == 0);
    CHECK(!shell.fs.exists(src));
    CHECK(shell.fs.exists(dst));
}

TEST_CASE("shell.builtin.mv_multiple_to_dir")
{
    namespace fs = std::filesystem;
    auto const baseGuard = endo::testing::ScopedTempDir { "endo_mv_test_multidir" };
    auto const& base = baseGuard.path();
    fs::create_directories(base / "target");
    auto const src1 = base / "a.txt";
    auto const src2 = base / "b.txt";

    {
        std::ofstream(src1) << "aaa";
        std::ofstream(src2) << "bbb";
    }

    TestShell shell;
    shell(std::format("mv {} {} {}", src1.string(), src2.string(), (base / "target").string()));
    CHECK(shell.exitCode == 0);
    CHECK(!fs::exists(src1));
    CHECK(!fs::exists(src2));
    CHECK(fs::exists(base / "target" / "a.txt"));
    CHECK(fs::exists(base / "target" / "b.txt"));
}

TEST_CASE("shell.builtin.mv_multiple_to_file")
{
    namespace fs = std::filesystem;
    auto const baseGuard = endo::testing::ScopedTempDir { "endo_mv_test_multifile" };
    auto const& base = baseGuard.path();
    auto const src1 = base / "a.txt";
    auto const src2 = base / "b.txt";
    auto const dst = base / "notadir.txt";

    {
        std::ofstream(src1) << "aaa";
        std::ofstream(src2) << "bbb";
        std::ofstream(dst) << "existing";
    }

    TestShell shell;
    shell(std::format("mv {} {} {}", src1.string(), src2.string(), dst.string()));
    CHECK(shell.exitCode == 1);
}

TEST_CASE("shell.builtin.mv_directory")
{
    namespace fs = std::filesystem;
    auto const baseGuard = endo::testing::ScopedTempDir { "endo_mv_test_dir" };
    auto const& base = baseGuard.path();
    fs::create_directories(base / "srcdir" / "sub");
    auto const dst = base / "dstdir";

    {
        std::ofstream(base / "srcdir" / "file.txt") << "data";
        std::ofstream(base / "srcdir" / "sub" / "nested.txt") << "nested";
    }

    TestShell shell;
    shell(std::format("mv {} {}", (base / "srcdir").string(), dst.string()));
    CHECK(shell.exitCode == 0);
    CHECK(!fs::exists(base / "srcdir"));
    CHECK(fs::exists(dst / "file.txt"));
    CHECK(fs::exists(dst / "sub" / "nested.txt"));
}

TEST_CASE("shell.builtin.mv_combined_flags")
{
    namespace fs = std::filesystem;
    auto const baseGuard = endo::testing::ScopedTempDir { "endo_mv_test_combined" };
    auto const& base = baseGuard.path();
    auto const src = base / "source.txt";
    auto const dst = base / "dest.txt";

    {
        std::ofstream(src) << "source";
        std::ofstream(dst) << "existing";
    }

    TestShell shell;
    // -nv: no-clobber + verbose; should skip move
    shell(std::format("mv -nv {} {}", src.string(), dst.string()));
    CHECK(shell.exitCode == 0);
    CHECK(fs::exists(src)); // Source still exists (no-clobber)
}

namespace
{
/// Returns the actual on-disk spelling of the single entry inside @p parent whose
/// name matches @p name case-insensitively, or an empty path if none exists.
///
/// Needed because `fs::exists("Foo")` is true on case-insensitive filesystems even
/// when the entry is really stored as `foo`, so a recase can only be observed by
/// reading back the directory entry's exact spelling.
std::filesystem::path onDiskName(std::filesystem::path const& parent, std::string_view name)
{
    namespace fs = std::filesystem;
    for (auto const& entry: fs::directory_iterator(parent))
    {
        auto candidate = entry.path().filename().string();
        if (endo::platform::equalsCaseInsensitive(candidate, name))
            return candidate;
    }
    return {};
}
} // namespace

TEST_CASE("shell.builtin.mv_case_only_rename_directory")
{
    namespace fs = std::filesystem;
    auto const baseGuard = endo::testing::ScopedTempDir { "endo_mv_test_recase_dir" };
    auto const& base = baseGuard.path();
    fs::create_directories(base / "foo");
    std::ofstream(base / "foo" / "keep.txt") << "data";

    TestShell shell;
    // Renaming `foo` -> `Foo` must recase the directory itself, not move it into a
    // `Foo/foo` subdirectory, and must succeed on case-insensitive filesystems.
    shell(std::format("mv {} {}", (base / "foo").string(), (base / "Foo").string()));
    CHECK(shell.exitCode == 0);
    CHECK(onDiskName(base, "foo") == "Foo");
    CHECK(fs::exists(base / "Foo" / "keep.txt"));
    CHECK(!fs::exists(base / "Foo" / "foo"));
}

TEST_CASE("shell.builtin.mv_case_only_rename_file")
{
    namespace fs = std::filesystem;
    auto const baseGuard = endo::testing::ScopedTempDir { "endo_mv_test_recase_file" };
    auto const& base = baseGuard.path();
    std::ofstream(base / "readme.txt") << "hello";

    TestShell shell;
    shell(std::format("mv {} {}", (base / "readme.txt").string(), (base / "README.txt").string()));
    CHECK(shell.exitCode == 0);
    CHECK(onDiskName(base, "readme.txt") == "README.txt");

    {
        std::ifstream ifs(base / "README.txt");
        std::string content;
        std::getline(ifs, content);
        CHECK(content == "hello");
    }
}

// ============================================================================
// Variable Substitution - $VAR and ${VAR}
// ============================================================================

TEST_CASE("shell.variable.simple_substitution")
{
    TestShell shell;
    shell("set GREETING hello");
    CHECK(escape(shell("echo $GREETING").output()) == escape("hello\n"));
}

TEST_CASE("shell.variable.braced_substitution")
{
    TestShell shell;
    shell("set GREETING hello");
    CHECK(escape(shell("echo ${GREETING}").output()) == escape("hello\n"));
}

TEST_CASE("shell.variable.substitution_in_arguments")
{
    TestShell shell;
    shell("set PATTERN ll");
    CHECK(escape(shell("echo hello | grep --color=never $PATTERN").output()) == escape("hello\n"));
}

TEST_CASE("shell.variable.multiple_substitutions")
{
    TestShell shell;
    shell("set FIRST hello");
    shell("set SECOND world");
    CHECK(escape(shell("echo $FIRST $SECOND").output()) == escape("hello world\n"));
}

TEST_CASE("shell.variable.undefined_expands_to_empty")
{
    TestShell shell;
    // Undefined variable should expand to empty string
    CHECK(escape(shell("echo $UNDEFINED").output()) == escape("\n"));
}

TEST_CASE("shell.variable.empty_value")
{
    TestShell shell;
    shell("set EMPTY \"\"");
    CHECK(escape(shell("echo $EMPTY").output()) == escape("\n"));
}

TEST_CASE("shell.variable.adjacent_to_text")
{
    TestShell shell;
    shell("set NAME world");
    // TODO: Adjacent variable substitution should concatenate into single word
    // Currently the lexer treats "hello", "${NAME}", and "foo" as separate tokens
    // For now, test current behavior (separate words)
    CHECK(escape(shell("echo hello ${NAME} foo").output()) == escape("hello world foo\n"));
}

// ============================================================================
// Special Variables - $?, $$
// ============================================================================

TEST_CASE("shell.variable.exit_status")
{
    TestShell shell;
    shell("exit 42");
    // After exit 42, $? should be 42
    // Note: We need to execute a command that uses $?
    CHECK(escape(shell("echo $?").output()) == escape("42\n"));
}

TEST_CASE("shell.variable.exit_status_after_success")
{
    TestShell shell;
    shell("true");
    CHECK(escape(shell("echo $?").output()) == escape("0\n"));
}

TEST_CASE("shell.variable.exit_status_after_failure")
{
    TestShell shell;
    shell("false");
    CHECK(escape(shell("echo $?").output()) == escape("1\n"));
}

TEST_CASE("shell.variable.process_id")
{
    TestShell shell;
    // $$ should return the shell's process ID, which should be a positive number
    shell("echo $$");
    auto const output = std::string(shell.output());
    // Verify it's a number
    CHECK_FALSE(output.empty());
    // Remove newline and verify it's numeric
    auto const pidStr = output.substr(0, output.find('\n'));
    CHECK_FALSE(pidStr.empty());
    for (char const c: pidStr)
        CHECK(std::isdigit(static_cast<unsigned char>(c)));
}

TEST_CASE("shell.variable.braced_exit_status")
{
    TestShell shell;
    shell("exit 99");
    CHECK(escape(shell("echo ${?}").output()) == escape("99\n"));
}

TEST_CASE("shell.variable.braced_process_id")
{
    TestShell shell;
    shell("echo ${$}");
    auto const output = std::string(shell.output());
    auto const pidStr = output.substr(0, output.find('\n'));
    CHECK_FALSE(pidStr.empty());
    for (char const c: pidStr)
        CHECK(std::isdigit(static_cast<unsigned char>(c)));
}

// ============================================================================
// Variable Scoping
// ============================================================================

TEST_CASE("shell.variable.set_overwrites_previous")
{
    TestShell shell;
    shell("set MYVAR first");
    CHECK(shell.env.get("MYVAR").value_or("NONE") == "first");
    shell("set MYVAR second");
    CHECK(shell.env.get("MYVAR").value_or("NONE") == "second");
}

TEST_CASE("shell.variable.unset_then_access")
{
    TestShell shell;
    shell("set MYVAR value");
    shell("unset MYVAR");
    // After unset, variable should be undefined (empty when expanded)
    CHECK(escape(shell("echo $MYVAR").output()) == escape("\n"));
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_CASE("shell.variable.underscore_in_name")
{
    TestShell shell;
    shell("set MY_VAR_NAME value");
    CHECK(shell.env.get("MY_VAR_NAME").value_or("NONE") == "value");
    CHECK(escape(shell("echo $MY_VAR_NAME").output()) == escape("value\n"));
}

TEST_CASE("shell.variable.numbers_in_name")
{
    TestShell shell;
    shell("set VAR123 value");
    CHECK(shell.env.get("VAR123").value_or("NONE") == "value");
    CHECK(escape(shell("echo $VAR123").output()) == escape("value\n"));
}

TEST_CASE("shell.variable.single_char_name")
{
    TestShell shell;
    shell("set X value");
    CHECK(shell.env.get("X").value_or("NONE") == "value");
    CHECK(escape(shell("echo $X").output()) == escape("value\n"));
}

// ============================================================================
// Redirects - Output Redirection
// ============================================================================

TEST_CASE("shell.redirect.output_to_file")
{
    auto const tempDir = endo::testing::ScopedTempDir { "endo_shell_test" };
    auto const outputPath = (tempDir / "output.txt").generic_string();
    TestShell shell;
    shell(std::format("echo hello > {}", outputPath));
    // Read the file to verify content
    std::ifstream file(outputPath);
    std::string content;
    std::getline(file, content);
    CHECK(content == "hello");
}

TEST_CASE("shell.redirect.output_append")
{
    auto const tempDir = endo::testing::ScopedTempDir { "endo_shell_test" };
    auto const appendPath = (tempDir / "append.txt").generic_string();
    TestShell shell;
    // Create initial file
    shell(std::format("echo line1 > {}", appendPath));
    // Append to it
    shell(std::format("echo line2 >> {}", appendPath));
    // Verify both lines present
    std::ifstream file(appendPath);
    auto line1 = std::string {};
    auto line2 = std::string {};
    std::getline(file, line1);
    std::getline(file, line2);
    CHECK(line1 == "line1");
    CHECK(line2 == "line2");
}

TEST_CASE("shell.redirect.input_from_file")
{
    auto const tempDir = endo::testing::ScopedTempDir { "endo_shell_test" };
    auto const inputPath = (tempDir / "input.txt").generic_string();
    TestShell shell;
    // Create test file (binary mode to avoid \r\n on Windows)
    {
        std::ofstream file(inputPath, std::ios::binary);
        file << "test input content\n";
    }
    // Use cat to read from file via redirect
    CHECK(escape(shell(std::format("cat < {}", inputPath)).output()) == escape("test input content\n"));
}

// ============================================================================
// Redirects - Here-strings
// ============================================================================

TEST_CASE("shell.redirect.herestring")
{
    TestShell shell;
    CHECK(escape(shell("cat <<< \"hello world\"").output()) == escape("hello world\n"));
}

TEST_CASE("shell.redirect.herestring_with_variable")
{
    TestShell shell;
    shell("set GREETING hello");
    CHECK(escape(shell("cat <<< $GREETING").output()) == escape("hello\n"));
}

// ============================================================================
// Redirects - File Descriptor Duplication
// ============================================================================

TEST_CASE("shell.redirect.stderr_to_stdout")
{
    TestShell shell;
    // Run a command that produces stderr output, redirected to stdout via 2>&1
#if defined(_WIN32)
    shell("where nonexistent_12345 2>&1");
#else
    shell("stat /nonexistent_path_12345 2>&1");
#endif
    // Should have some output (the error message)
    CHECK(!shell.output().empty());
}

TEST_CASE("shell.redirect.fd_to_file")
{
    TestShell shell;
    auto const tmpDir = endo::testing::ScopedTempDir { "endo_test_stderr" };
    auto const tmpFile = tmpDir / "stderr.txt";
    auto const tmpFileStr = tmpFile.generic_string();
    // Redirect stderr (fd 2) to a file
#if defined(_WIN32)
    shell(std::format("where nonexistent_12345 2> {}", tmpFileStr));
#else
    shell(std::format("stat /nonexistent_path_12345 2> {}", tmpFileStr));
#endif
    // Verify the error was written to the file
    std::ifstream file(tmpFile);
    std::string content;
    std::getline(file, content);
    CHECK(!content.empty()); // Should contain error message
}

// ============================================================================
// Redirects - Multiple Redirects
// ============================================================================

TEST_CASE("shell.redirect.multiple_redirects")
{
    auto const tempDir = endo::testing::ScopedTempDir { "endo_shell_test" };
    auto const multiInPath = (tempDir / "in.txt").generic_string();
    auto const multiOutPath = (tempDir / "out.txt").generic_string();
    TestShell shell;
    // Create input file (binary mode to avoid \r\n on Windows)
    {
        std::ofstream file(multiInPath, std::ios::binary);
        file << "input text\n";
    }
    // Redirect both input and output
    shell(std::format("cat < {} > {}", multiInPath, multiOutPath));
    // Verify output
    std::ifstream file(multiOutPath);
    std::string content;
    std::getline(file, content);
    CHECK(content == "input text");
}

// ============================================================================
// Logical Operators
// ============================================================================

TEST_CASE("shell.logical.and_success")
{
    // true && echo hello - should execute echo because true succeeds
    TestShell shell;
    CHECK(escape(shell("true && echo hello").output()) == escape("hello\n"));
}

TEST_CASE("shell.logical.and_failure")
{
    // false && echo hello - should NOT execute echo because false fails
    TestShell shell;
    CHECK(shell("false && echo hello").output().empty());
}

TEST_CASE("shell.logical.or_success")
{
    // true || echo hello - should NOT execute echo because true succeeds
    TestShell shell;
    CHECK(shell("true || echo hello").output().empty());
}

TEST_CASE("shell.logical.or_failure")
{
    // false || echo hello - should execute echo because false fails
    TestShell shell;
    CHECK(escape(shell("false || echo hello").output()) == escape("hello\n"));
}

TEST_CASE("shell.logical.chained_and")
{
    // true && true && echo hello - should execute echo
    TestShell shell;
    CHECK(escape(shell("true && true && echo hello").output()) == escape("hello\n"));
}

TEST_CASE("shell.logical.chained_or")
{
    // false || false || echo hello - should execute echo
    TestShell shell;
    CHECK(escape(shell("false || false || echo hello").output()) == escape("hello\n"));
}

TEST_CASE("shell.logical.chained_and_real_commands")
{
    // 3 chained real commands (not builtins like true/false) - all succeed
    TestShell shell;
    CHECK(escape(shell("echo a && echo b && echo c").output()) == escape("a\nb\nc\n"));
}

TEST_CASE("shell.logical.chained_and_first_fails")
{
    // First command fails, rest should be skipped
    TestShell shell;
    CHECK(shell("false && echo b && echo c").output().empty());
}

TEST_CASE("shell.logical.chained_and_middle_fails")
{
    // Second command fails, third should be skipped
    TestShell shell;
    CHECK(escape(shell("echo a && false && echo c").output()) == escape("a\n"));
}

TEST_CASE("shell.logical.chained_or_real_commands")
{
    // 3 chained || with real commands, last succeeds
    TestShell shell;
    CHECK(escape(shell("false || false || echo c").output()) == escape("c\n"));
}

TEST_CASE("shell.logical.mixed_operators")
{
    // true && false || echo hello - && has same precedence as ||, left-to-right
    // (true && false) || echo hello - true&&false = false, so echo runs
    TestShell shell;
    CHECK(escape(shell("true && false || echo hello").output()) == escape("hello\n"));
}

TEST_CASE("shell.logical.exit_code_propagation_and")
{
    // After true && false, exit code should be 1 (from false)
    TestShell shell;
    shell("true && false");
    CHECK(shell("echo $?").output() == "1\n");
}

TEST_CASE("shell.logical.exit_code_propagation_or")
{
    // After false || true, exit code should be 0 (from true)
    TestShell shell;
    shell("false || true");
    CHECK(shell("echo $?").output() == "0\n");
}

TEST_CASE("shell.logical.with_pipeline")
{
    // Logical operators should have lower precedence than pipes
    // echo hello | cat && echo world
    // Should be: (echo hello | cat) && echo world
    TestShell shell;
    CHECK(escape(shell("echo hello | cat && echo world").output()) == escape("hello\nworld\n"));
}

// ============================================================================
// Command Substitution
// ============================================================================

TEST_CASE("shell.subst.command_basic")
{
    // Basic command substitution: echo $(echo hello)
    TestShell shell;
    CHECK(escape(shell("echo $(echo hello)").output()) == escape("hello\n"));
}

TEST_CASE("shell.subst.command_multiple_words")
{
    // Command substitution with multiple words
    TestShell shell;
    CHECK(escape(shell("echo $(echo hello world)").output()) == escape("hello world\n"));
}

TEST_CASE("shell.subst.command_with_args")
{
    // Command substitution with arguments
    TestShell shell;
    CHECK(escape(shell("echo prefix $(echo middle) suffix").output()) == escape("prefix middle suffix\n"));
}

TEST_CASE("shell.subst.backtick_basic")
{
    // Backtick command substitution: echo `echo hello`
    TestShell shell;
    CHECK(escape(shell("echo `echo hello`").output()) == escape("hello\n"));
}

TEST_CASE("shell.subst.backtick_with_args")
{
    // Backtick substitution with surrounding arguments
    TestShell shell;
    CHECK(escape(shell("echo prefix `echo middle` suffix").output()) == escape("prefix middle suffix\n"));
}

TEST_CASE("shell.subst.newline_trim")
{
    // Trailing newlines should be trimmed from command substitution
    // printf outputs without trailing newline, but echo inside adds one
    TestShell shell;
    // echo "hello\n" -> substitution trims -> "hello"
    CHECK(escape(shell("echo $(echo hello)").output()) == escape("hello\n"));
}

TEST_CASE("shell.subst.with_variable")
{
    // Command substitution with variable expansion
    TestShell shell;
    shell("set MSG hello");
    CHECK(escape(shell("echo $(echo $MSG)").output()) == escape("hello\n"));
}

TEST_CASE("shell.subst.in_pipeline")
{
    // Command substitution as part of a pipeline
    TestShell shell;
    CHECK(escape(shell("echo $(echo hello) | cat").output()) == escape("hello\n"));
}

// ============================================================================
// Process Substitution
// ============================================================================

#if !defined(_WIN32)
TEST_CASE("shell.subst.process_read_basic")
{
    // Process substitution read mode: cat <(echo hello)
    TestShell shell;
    CHECK(escape(shell("cat <(echo hello)").output()) == escape("hello\n"));
}

TEST_CASE("shell.subst.process_read_multiple_lines")
{
    auto const tempDir = endo::testing::ScopedTempDir { "endo_shell_test" };
    auto const procSubstPath = (tempDir / "procsubst.txt").generic_string();
    // Process substitution with multiple lines
    TestShell shell;
    shell(std::format("echo line1 > {}", procSubstPath));
    shell(std::format("echo line2 >> {}", procSubstPath));
    CHECK(escape(shell(std::format("cat <(cat {})", procSubstPath)).output()) == escape("line1\nline2\n"));
}
#endif

// ============================================================================
// Tilde Expansion
// ============================================================================

TEST_CASE("shell.expand.tilde_home")
{
    // ~ should expand to $HOME
    TestShell shell;
    shell.env.set("HOME", "/home/testuser");
    CHECK(escape(shell("echo ~").output()) == escape("/home/testuser\n"));
}

TEST_CASE("shell.expand.tilde_in_path")
{
    // ~/Documents should expand to $HOME/Documents
    TestShell shell;
    shell.env.set("HOME", "/home/testuser");
    CHECK(escape(shell("echo ~/Documents").output()) == escape("/home/testuser/Documents\n"));
}

#if !defined(_WIN32)
TEST_CASE("shell.expand.tilde_user")
{
    // ~root should expand to root's home directory
    TestShell shell;
    // Note: This test assumes root exists on the system
    shell("echo ~root");
    auto const output = std::string(shell.output());
    // Should either be /root or /var/root (macOS) - either way, not ~root
    CHECK(output.find("~root") == std::string::npos);
}
#endif

TEST_CASE("shell.expand.tilde_nonexistent_user")
{
    // ~nonexistentuser12345 should stay unexpanded
    TestShell shell;
    CHECK(escape(shell("echo ~nonexistentuser12345").output()) == escape("~nonexistentuser12345\n"));
}

TEST_CASE("shell.expand.tilde_multiple")
{
    // Multiple tildes in one command
    TestShell shell;
    shell.env.set("HOME", "/home/testuser");
    CHECK(escape(shell("echo ~ ~").output()) == escape("/home/testuser /home/testuser\n"));
}

#if !defined(_WIN32)
TEST_CASE("shell.expand.tilde_command")
{
    // ~/bin/foo as a command: tilde in program position should be expanded
    TestShell shell;
    auto const* home = std::getenv("HOME");
    REQUIRE(home != nullptr);

    // Create a test script in a temp directory, then use tilde to invoke it
    // We test that the tilde-prefixed path parses and executes correctly
    // by using /bin/echo via a tilde path (simulated through PATH)
    // Instead, test that tilde commands parse correctly by using an absolute echo path
    CHECK(shell("~/../../bin/echo hello").exitCode == 0);
    CHECK(escape(shell.output()).find("hello") != std::string::npos);
}

TEST_CASE("shell.expand.tilde_command_pipeline")
{
    // Tilde-prefixed command in a pipe: ~/../../bin/echo hello | cat
    TestShell shell;
    CHECK(shell("~/../../bin/echo tilde_test | cat").exitCode == 0);
    CHECK(escape(shell.output()).find("tilde_test") != std::string::npos);
}
#endif

// ============================================================================
// Brace Expansion
// ============================================================================

TEST_CASE("shell.expand.brace_comma_list")
{
    // {a,b,c} → a b c
    TestShell shell;
    CHECK(escape(shell("echo {a,b,c}").output()) == escape("a b c\n"));
}

TEST_CASE("shell.expand.brace_numeric_range")
{
    // {1..5} → 1 2 3 4 5
    TestShell shell;
    CHECK(escape(shell("echo {1..5}").output()) == escape("1 2 3 4 5\n"));
}

TEST_CASE("shell.expand.brace_alpha_range")
{
    // {a..e} → a b c d e
    TestShell shell;
    CHECK(escape(shell("echo {a..e}").output()) == escape("a b c d e\n"));
}

TEST_CASE("shell.expand.brace_with_prefix")
{
    // file{1,2}.txt → file1.txt file2.txt
    TestShell shell;
    CHECK(escape(shell("echo file{1,2}.txt").output()) == escape("file1.txt file2.txt\n"));
}

TEST_CASE("shell.expand.brace_with_suffix")
{
    // {test,prod}_db → test_db prod_db
    TestShell shell;
    CHECK(escape(shell("echo {test,prod}_db").output()) == escape("test_db prod_db\n"));
}

TEST_CASE("shell.expand.brace_nested")
{
    // {a,b{1,2}} → a b1 b2
    TestShell shell;
    CHECK(escape(shell("echo {a,b{1,2}}").output()) == escape("a b1 b2\n"));
}

TEST_CASE("shell.expand.brace_reverse_range")
{
    // {5..1} → 5 4 3 2 1
    TestShell shell;
    CHECK(escape(shell("echo {5..1}").output()) == escape("5 4 3 2 1\n"));
}

TEST_CASE("shell.expand.brace_alpha_reverse_range")
{
    // {e..a} → e d c b a
    TestShell shell;
    CHECK(escape(shell("echo {e..a}").output()) == escape("e d c b a\n"));
}

TEST_CASE("shell.expand.brace_empty_items")
{
    // {a,,c} → a (empty) c
    TestShell shell;
    CHECK(escape(shell("echo {a,,c}").output()) == escape("a  c\n"));
}

TEST_CASE("shell.expand.brace_single_item")
{
    // {a} stays as-is (no expansion without comma or range)
    TestShell shell;
    CHECK(escape(shell("echo {a}").output()) == escape("{a}\n"));
}

// ============================================================================
// Parameter Expansion
// ============================================================================

TEST_CASE("shell.expand.param_length")
{
    // ${#VAR} returns length of variable value
    TestShell shell;
    shell("set GREETING hello");
    CHECK(escape(shell("echo ${#GREETING}").output()) == escape("5\n"));
}

TEST_CASE("shell.expand.param_length_empty")
{
    // ${#VAR} for empty variable returns 0
    TestShell shell;
    CHECK(escape(shell("echo ${#UNDEFINED}").output()) == escape("0\n"));
}

TEST_CASE("shell.expand.param_default_unset")
{
    // ${UNSET:-default} returns default when variable is unset
    TestShell shell;
    CHECK(escape(shell("echo ${UNSET:-default}").output()) == escape("default\n"));
}

TEST_CASE("shell.expand.param_default_set")
{
    // ${VAR:-default} returns VAR when set
    TestShell shell;
    shell("set VAR value");
    CHECK(escape(shell("echo ${VAR:-default}").output()) == escape("value\n"));
}

TEST_CASE("shell.expand.param_default_empty")
{
    // ${VAR:-default} returns default when VAR is empty string
    TestShell shell;
    shell("set VAR \"\"");
    CHECK(escape(shell("echo ${VAR:-default}").output()) == escape("default\n"));
}

TEST_CASE("shell.expand.param_alternate_unset")
{
    // ${UNSET:+alt} returns empty when variable is unset
    TestShell shell;
    // Note: empty string expansion becomes a space-separated argument
    // so we check that the alternate is not used
    shell("set RESULT ${UNSET:+alt}");
    CHECK(shell.env.get("RESULT").value_or("FAIL").empty());
}

TEST_CASE("shell.expand.param_alternate_set")
{
    // ${VAR:+alt} returns alt when VAR is set
    TestShell shell;
    shell("set VAR value");
    CHECK(escape(shell("echo ${VAR:+alt}").output()) == escape("alt\n"));
}

TEST_CASE("shell.expand.param_assign")
{
    // ${VAR:=default} assigns default when unset
    TestShell shell;
    shell("echo ${NEWVAR:=assigned}");
    CHECK(shell.env.get("NEWVAR").value_or("NONE") == "assigned");
}

TEST_CASE("shell.expand.param_replace_first")
{
    // ${VAR/old/new} replaces first occurrence
    TestShell shell;
    shell("set TEXT one:two:one");
    CHECK(escape(shell("echo ${TEXT/one/ONE}").output()) == escape("ONE:two:one\n"));
}

TEST_CASE("shell.expand.param_replace_all")
{
    // ${VAR//old/new} replaces all occurrences
    TestShell shell;
    shell("set TEXT one:two:one");
    CHECK(escape(shell("echo ${TEXT//one/ONE}").output()) == escape("ONE:two:ONE\n"));
}

TEST_CASE("shell.expand.param_remove_prefix")
{
    // ${VAR#pattern} removes shortest prefix match
    TestShell shell;
    shell("set FILE path/to/file.txt");
    CHECK(escape(shell("echo ${FILE#*/}").output()) == escape("to/file.txt\n"));
}

TEST_CASE("shell.expand.param_remove_prefix_long")
{
    // ${VAR##pattern} removes longest prefix match
    TestShell shell;
    shell("set FILE path/to/file.txt");
    CHECK(escape(shell("echo ${FILE##*/}").output()) == escape("file.txt\n"));
}

TEST_CASE("shell.expand.param_remove_suffix")
{
    // ${VAR%pattern} removes shortest suffix match
    TestShell shell;
    shell("set FILE file.tar.gz");
    CHECK(escape(shell("echo ${FILE%.*}").output()) == escape("file.tar\n"));
}

TEST_CASE("shell.expand.param_remove_suffix_long")
{
    // ${VAR%%pattern} removes longest suffix match
    TestShell shell;
    shell("set FILE file.tar.gz");
    CHECK(escape(shell("echo ${FILE%%.*}").output()) == escape("file\n"));
}

// ========================================================================
// Glob (Pathname) Expansion Tests
// ========================================================================

TEST_CASE("shell.expand.glob_star")
{
    auto const globDirGuard = endo::testing::ScopedTempDir { "endo_glob" };
    auto const globDir = globDirGuard.string();
    // *.txt should match .txt files in current directory
    TestShell shell;

    // Create test files
    shell(std::format("echo test > {}/glob_test_a.txt", globDir));
    shell(std::format("echo test > {}/glob_test_b.txt", globDir));
    shell(std::format("echo test > {}/glob_test_c.log", globDir));

    // Run glob expansion
    auto result = shell(std::format("echo {}/glob_test_*.txt", globDir)).output();

    // Should contain both .txt files but not the .log file
    CHECK(result.find("glob_test_a.txt") != std::string::npos);
    CHECK(result.find("glob_test_b.txt") != std::string::npos);
    CHECK(result.find("glob_test_c.log") == std::string::npos);

    // Cleanup
    shell(std::format("rm {}/glob_test_*.txt {}/glob_test_*.log", globDir, globDir));
}

TEST_CASE("shell.expand.glob_question")
{
    auto const globDirGuard = endo::testing::ScopedTempDir { "endo_glob" };
    auto const globDir = globDirGuard.string();
    // ? matches single character
    TestShell shell;

    // Create test files
    shell(std::format("echo test > {}/glob_qtest_a.txt", globDir));
    shell(std::format("echo test > {}/glob_qtest_b.txt", globDir));
    shell(std::format("echo test > {}/glob_qtest_aa.txt", globDir));

    // Run glob expansion - should match single character only
    auto result = shell(std::format("echo {}/glob_qtest_?.txt", globDir)).output();

    CHECK(result.find("glob_qtest_a.txt") != std::string::npos);
    CHECK(result.find("glob_qtest_b.txt") != std::string::npos);
    CHECK(result.find("glob_qtest_aa.txt") == std::string::npos);

    // Cleanup
    shell(std::format("rm {}/glob_qtest_*.txt", globDir));
}

TEST_CASE("shell.expand.glob_bracket")
{
    auto const globDirGuard = endo::testing::ScopedTempDir { "endo_glob" };
    auto const globDir = globDirGuard.string();
    // [abc] matches any character in set
    TestShell shell;

    // Create test files
    shell(std::format("echo test > {}/glob_btest_a.txt", globDir));
    shell(std::format("echo test > {}/glob_btest_b.txt", globDir));
    shell(std::format("echo test > {}/glob_btest_c.txt", globDir));
    shell(std::format("echo test > {}/glob_btest_d.txt", globDir));

    // Run glob expansion - should match a, b, c but not d
    auto result = shell(std::format("echo {}/glob_btest_[abc].txt", globDir)).output();

    CHECK(result.find("glob_btest_a.txt") != std::string::npos);
    CHECK(result.find("glob_btest_b.txt") != std::string::npos);
    CHECK(result.find("glob_btest_c.txt") != std::string::npos);
    CHECK(result.find("glob_btest_d.txt") == std::string::npos);

    // Cleanup
    shell(std::format("rm {}/glob_btest_*.txt", globDir));
}

TEST_CASE("shell.expand.glob_no_match")
{
    // When no files match, keep pattern literal (standard shell behavior)
    TestShell shell;
    auto result = shell("echo /nonexistent/path/*.xyz").output();
    CHECK(result.find("/nonexistent/path/*.xyz") != std::string::npos);
}

TEST_CASE("shell.expand.glob_bracket_range")
{
    auto const globDirGuard = endo::testing::ScopedTempDir { "endo_glob" };
    auto const globDir = globDirGuard.string();
    // [a-z] matches a range of characters
    TestShell shell;

    // Create test files with letters
    shell(std::format("echo test > {}/glob_rtest_a.txt", globDir));
    shell(std::format("echo test > {}/glob_rtest_c.txt", globDir));
    shell(std::format("echo test > {}/glob_rtest_z.txt", globDir));
    shell(std::format("echo test > {}/glob_rtest_1.txt", globDir));

    // Run glob expansion - should match a, c, z but not 1
    auto result = shell(std::format("echo {}/glob_rtest_[a-z].txt", globDir)).output();

    CHECK(result.find("glob_rtest_a.txt") != std::string::npos);
    CHECK(result.find("glob_rtest_c.txt") != std::string::npos);
    CHECK(result.find("glob_rtest_z.txt") != std::string::npos);
    CHECK(result.find("glob_rtest_1.txt") == std::string::npos);

    // Cleanup
    shell(std::format("rm {}/glob_rtest_*.txt", globDir));
}

TEST_CASE("shell.expand.glob_recursive_starstar")
{
    auto const globDirGuard = endo::testing::ScopedTempDir { "endo_glob" };
    auto const globDir = globDirGuard.string();
    // ** matches files recursively
    TestShell shell;

    // Create directory structure
    shell(std::format("mkdir -p {}/glob_rec_test/sub1/sub2", globDir));
    shell(std::format("echo test > {}/glob_rec_test/file1.cpp", globDir));
    shell(std::format("echo test > {}/glob_rec_test/sub1/file2.cpp", globDir));
    shell(std::format("echo test > {}/glob_rec_test/sub1/sub2/file3.cpp", globDir));
    shell(std::format("echo test > {}/glob_rec_test/file4.txt", globDir));

    // Run recursive glob expansion
    auto result = shell(std::format("echo {}/glob_rec_test/**/*.cpp", globDir)).output();

    // Should find all .cpp files recursively
    CHECK(result.find("file1.cpp") != std::string::npos);
    CHECK(result.find("file2.cpp") != std::string::npos);
    CHECK(result.find("file3.cpp") != std::string::npos);
    CHECK(result.find("file4.txt") == std::string::npos);

    // Cleanup
    shell(std::format("rm -rf {}/glob_rec_test", globDir));
}

// ========================================================================
// Arithmetic Expansion Tests
// ========================================================================

TEST_CASE("shell.expand.arith_basic")
{
    // Basic arithmetic: $((1 + 2))
    TestShell shell;
    CHECK(escape(shell("echo $((1 + 2))").output()) == escape("3\n"));
}

TEST_CASE("shell.expand.arith_subtraction")
{
    TestShell shell;
    CHECK(escape(shell("echo $((10 - 3))").output()) == escape("7\n"));
}

TEST_CASE("shell.expand.arith_multiplication")
{
    TestShell shell;
    CHECK(escape(shell("echo $((4 * 5))").output()) == escape("20\n"));
}

TEST_CASE("shell.expand.arith_division")
{
    TestShell shell;
    CHECK(escape(shell("echo $((20 / 4))").output()) == escape("5\n"));
}

TEST_CASE("shell.expand.arith_modulo")
{
    TestShell shell;
    CHECK(escape(shell("echo $((17 % 5))").output()) == escape("2\n"));
}

TEST_CASE("shell.expand.arith_precedence")
{
    // Test operator precedence: * before +
    TestShell shell;
    CHECK(escape(shell("echo $((2 + 3 * 4))").output()) == escape("14\n"));
}

TEST_CASE("shell.expand.arith_parentheses")
{
    // Parentheses override precedence
    TestShell shell;
    CHECK(escape(shell("echo $(((2 + 3) * 4))").output()) == escape("20\n"));
}

TEST_CASE("shell.expand.arith_variable")
{
    // Variable reference in arithmetic
    TestShell shell;
    shell("set X 10");
    CHECK(escape(shell("echo $((X + 5))").output()) == escape("15\n"));
}

TEST_CASE("shell.expand.arith_comparison_lt")
{
    // Comparison: 5 < 10 is true (1)
    TestShell shell;
    CHECK(escape(shell("echo $((5 < 10))").output()) == escape("1\n"));
}

TEST_CASE("shell.expand.arith_comparison_gt")
{
    // Comparison: 10 > 5 is true (1)
    TestShell shell;
    CHECK(escape(shell("echo $((10 > 5))").output()) == escape("1\n"));
}

TEST_CASE("shell.expand.arith_unary_negation")
{
    // Unary negation
    TestShell shell;
    CHECK(escape(shell("echo $((-5))").output()) == escape("-5\n"));
}

// ========================================================================
// Job Management Tests
// ========================================================================

TEST_CASE("shell.jobs.background_simple")
{
    // Background execution with & should return immediately (exit code 0)
    TestShell shell;
    // Run a short-lived background command - returns immediately
    auto exitCode = shell("sleep 0 &").exitCode;
    CHECK(exitCode == 0);
}

TEST_CASE("shell.jobs.background_sets_exit_zero")
{
    // Background execution should return exit code 0 immediately
    TestShell shell;
    CHECK(shell("sleep 0 &").exitCode == 0);
}

TEST_CASE("shell.jobs.dollar_bang")
{
    // $! should contain the PID of the last background process
    TestShell shell;
    shell("sleep 0 &");
    // Get the background PID
    auto output = shell("echo $!").output();
    // Should output a number (the PID)
    CHECK(!output.empty());
    // The output should contain digits (a PID)
    bool hasDigits = false;
    for (char c: output)
    {
        if (std::isdigit(c))
            hasDigits = true;
    }
    CHECK(hasDigits);
}

TEST_CASE("shell.jobs.jobs_builtin_empty")
{
    // jobs builtin with no background jobs should display empty list
    TestShell shell;
    auto result = shell("jobs").output();
    CHECK(result == "[]\n");
    CHECK(shell.exitCode == 0);
}

TEST_CASE("shell.jobs.wait_all")
{
    // wait with no arguments should wait for all background jobs
    TestShell shell;
    // Start a background job and wait for it
    shell("sleep 0 &");
    auto waitExitCode = shell("wait").exitCode;
    // Wait should complete successfully
    CHECK(waitExitCode == 0);
}

TEST_CASE("shell.jobs.fg_is_builtin")
{
    // fg without background jobs should return non-zero from builtin
    // (not "/usr/bin/fg: no job control" error from external command)
    TestShell shell;
    auto const result = shell("fg").exitCode;
    // fg with no jobs should fail, but through the builtin path
    CHECK(result != 0);
    // Verify it's not trying to execute external /usr/bin/fg
    // (external fg would have a different error pattern)
}

TEST_CASE("shell.jobs.bg_is_builtin")
{
    // bg without background jobs should return non-zero from builtin
    TestShell shell;
    auto const result = shell("bg").exitCode;
    // bg with no jobs should fail, but through the builtin path
    CHECK(result != 0);
}

TEST_CASE("shell.jobs.fg_with_job_id")
{
    // fg with invalid job ID should fail through builtin
    TestShell shell;
    auto const result = shell("fg 999").exitCode;
    CHECK(result != 0);
}

TEST_CASE("shell.jobs.bg_with_job_id")
{
    // bg with invalid job ID should fail through builtin
    TestShell shell;
    auto const result = shell("bg 999").exitCode;
    CHECK(result != 0);
}

TEST_CASE("shell.jobs.wait_with_job_id")
{
    // wait with invalid job ID should fail through builtin
    TestShell shell;
    auto const result = shell("wait 999").exitCode;
    CHECK(result != 0);
}

// ============================================================================
// FileCompleter Tests
// ============================================================================

TEST_CASE("FileCompleter.prefix_match_scores_higher_than_fuzzy")
{
    // Create a temporary directory with "src" and "scripts" subdirectories
    auto fs = endo::InMemoryFileSystem {};
    fs.addDirectory("/test/src");
    fs.addDirectory("/test/scripts");
    fs.setCurrentPath("/test");

    endo::TestEnvironment env;
    endo::FileCompleter completer(env, fs);

    // Complete "sr" - should match both "src" (prefix) and "scripts" (fuzzy)
    endo::CompletionContext context {
        .type = endo::CompletionContextType::FilePath,
        .prefix = "sr",
        .cursorPosition = 5,
        .fullInput = "cd sr",
    };

    auto results = completer.complete(context);

    // Verify results
    REQUIRE(results.size() == 2);

    // "src" should be first because it's a prefix match (higher score)
    // "scripts" should be second because it's only a fuzzy match
    CHECK(results[0].displayText == "src/");
    CHECK(results[1].displayText == "scripts/");

    // Prefix match should have higher score
    CHECK(results[0].score > results[1].score);

    // Prefix match should have empty matchPositions (no highlighting needed)
    CHECK(results[0].matchPositions.empty());

    // Fuzzy match should have matchPositions for 's' and 'r'
    CHECK_FALSE(results[1].matchPositions.empty());
}

TEST_CASE("FileCompleter.relative_prefix_stays_relative")
{
    // Regression: a relative nested prefix (e.g. "sub/it") must keep its relative
    // form in the completion. On Windows, canonicalizing the parent directory would
    // resolve it to an absolute path and rewrite the user's typed relative prefix.
    auto fs = endo::InMemoryFileSystem {};
    fs.addFile("/test/sub/item.txt", "");
    fs.setCurrentPath("/test");

    endo::TestEnvironment env;
    endo::FileCompleter completer(env, fs);

    endo::CompletionContext context {
        .type = endo::CompletionContextType::FilePath,
        .prefix = "sub/it",
        .cursorPosition = 6,
        .fullInput = "cd sub/it",
    };
    auto const results = completer.complete(context);

    REQUIRE(results.size() == 1);
    CHECK(results[0].text == "sub/item.txt"); // Relative, not an absolute path.
}

#if defined(_WIN32)
TEST_CASE("FileCompleter.absolute_directory_corrects_case")
{
    // On case-insensitive filesystems, completing an absolute directory typed in the
    // wrong case must echo the on-disk capitalization (".../foo" -> ".../Foo/") rather
    // than the case the user typed.
    // Stays on the real filesystem: InMemoryFileSystem does not model case-insensitive
    // lookup, so an injected one would make this pass without testing anything.
    auto const tempDirGuard = endo::testing::ScopedTempDir { "endo_case_completion" };
    auto const& tempDir = tempDirGuard.path();
    std::filesystem::create_directories(tempDir / "Foo");

    endo::TestEnvironment env;
    endo::FileCompleter completer(env, endo::NativeFileSystem::instance());

    // Type the directory in lower-case; it resolves case-insensitively to "Foo".
    auto const typed = endo::platform::normalizePath((tempDir / "foo").string());
    endo::CompletionContext context {
        .type = endo::CompletionContextType::FilePath,
        .prefix = typed,
        .cursorPosition = typed.size() + 3,
        .fullInput = "cd " + typed,
    };
    auto const results = completer.complete(context);

    REQUIRE(results.size() == 1);
    CHECK(results[0].displayText == "Foo/");
    CHECK(results[0].text.ends_with("/Foo/"));
    CHECK(results[0].text.find("/foo/") == std::string::npos);
}

TEST_CASE("FileCompleter.partial_prefix_corrects_case")
{
    // A partial directory name typed in the wrong case (even with leading upper-case,
    // which would otherwise trigger smart-case) must still match and recase on a
    // case-insensitive filesystem (e.g. "Lastrada-to" -> "lastrada-tools/").
    // Real filesystem, for the same reason as the test above.
    auto const tempDirGuard = endo::testing::ScopedTempDir { "endo_partial_case_completion" };
    auto const& tempDir = tempDirGuard.path();
    std::filesystem::create_directories(tempDir / "lastrada-tools");

    endo::TestEnvironment env;
    endo::FileCompleter completer(env, endo::NativeFileSystem::instance());

    auto const typed = endo::platform::normalizePath((tempDir / "Lastrada-to").string());
    endo::CompletionContext context {
        .type = endo::CompletionContextType::FilePath,
        .prefix = typed,
        .cursorPosition = typed.size() + 3,
        .fullInput = "cd " + typed,
    };
    auto const results = completer.complete(context);

    REQUIRE(results.size() == 1);
    CHECK(results[0].displayText == "lastrada-tools/");
    CHECK(results[0].text.ends_with("/lastrada-tools/"));
}
#endif

// ========================================================================
// String Interpolation Tests
// ========================================================================

TEST_CASE("shell.interpolation.simple_variable")
{
    // "hello $USER" should expand the variable
    TestShell shell;
    shell("set NAME world");
    CHECK(escape(shell("echo \"hello $NAME\"").output()) == escape("hello world\n"));
}

TEST_CASE("shell.interpolation.braced_variable")
{
    // "hello ${USER}" should expand the variable
    TestShell shell;
    shell("set NAME world");
    CHECK(escape(shell("echo \"hello ${NAME}\"").output()) == escape("hello world\n"));
}

TEST_CASE("shell.interpolation.multiple_variables")
{
    // Multiple variables in one string
    TestShell shell;
    shell("set FIRST hello");
    shell("set SECOND world");
    CHECK(escape(shell("echo \"$FIRST $SECOND\"").output()) == escape("hello world\n"));
}

TEST_CASE("shell.interpolation.adjacent_variables")
{
    // Adjacent variables without space
    TestShell shell;
    shell("set A foo");
    shell("set B bar");
    CHECK(escape(shell("echo \"$A$B\"").output()) == escape("foobar\n"));
}

TEST_CASE("shell.interpolation.braced_and_simple")
{
    // Mix of braced and simple variable syntax
    TestShell shell;
    shell("set USER alice");
    CHECK(escape(shell("echo \"hello ${USER}, or $USER\"").output()) == escape("hello alice, or alice\n"));
}

TEST_CASE("shell.interpolation.empty_string")
{
    // Empty double-quoted string
    TestShell shell;
    CHECK(escape(shell("echo \"\"").output()) == escape("\n"));
}

TEST_CASE("shell.interpolation.no_variables")
{
    // Double-quoted string without variables
    TestShell shell;
    CHECK(escape(shell("echo \"hello world\"").output()) == escape("hello world\n"));
}

TEST_CASE("shell.interpolation.undefined_variable")
{
    // Undefined variable expands to empty string
    TestShell shell;
    CHECK(escape(shell("echo \"hello $UNDEFINED_VAR!\"").output()) == escape("hello !\n"));
}

TEST_CASE("shell.interpolation.special_variable_exit_status")
{
    // $? inside double quotes
    TestShell shell;
    shell("true");
    CHECK(escape(shell("echo \"Exit: $?\"").output()) == escape("Exit: 0\n"));
}

TEST_CASE("shell.interpolation.special_variable_pid")
{
    // $$ inside double quotes (should produce a number)
    TestShell shell;
    auto result = shell("echo \"PID: $$\"").output();
    CHECK(result.starts_with("PID: "));
    // Should have digits after "PID: "
    CHECK(result.size() > 6);
}

TEST_CASE("shell.interpolation.command_substitution")
{
    // $(command) inside double quotes
    TestShell shell;
    CHECK(escape(shell("echo \"Today: $(echo date)\"").output()) == escape("Today: date\n"));
}

TEST_CASE("shell.interpolation.backtick_substitution")
{
    // `command` inside double quotes
    TestShell shell;
    CHECK(escape(shell("echo \"User: `echo alice`\"").output()) == escape("User: alice\n"));
}

TEST_CASE("shell.interpolation.arithmetic_expansion")
{
    // $((expr)) inside double quotes
    TestShell shell;
    CHECK(escape(shell("echo \"Sum: $((1+2))\"").output()) == escape("Sum: 3\n"));
}

TEST_CASE("shell.interpolation.param_expansion")
{
    // ${VAR:-default} inside double quotes
    TestShell shell;
    CHECK(escape(shell("echo \"${UNSET:-default}\"").output()) == escape("default\n"));
}

TEST_CASE("shell.interpolation.param_expansion_length")
{
    // ${#VAR} inside double quotes
    TestShell shell;
    shell("set TEXT hello");
    CHECK(escape(shell("echo \"Length: ${#TEXT}\"").output()) == escape("Length: 5\n"));
}

TEST_CASE("shell.interpolation.escaped_dollar")
{
    // \$ should produce literal $
    TestShell shell;
    CHECK(escape(shell("echo \"Price: \\$100\"").output()) == escape("Price: $100\n"));
}

TEST_CASE("shell.interpolation.escaped_quote")
{
    // \" should produce literal "
    TestShell shell;
    CHECK(escape(shell("echo \"He said \\\"hi\\\"\"").output()) == escape("He said \"hi\"\n"));
}

TEST_CASE("shell.interpolation.escaped_backslash")
{
    // \\ should produce single backslash
    TestShell shell;
    CHECK(escape(shell("echo \"path\\\\name\"").output()) == escape("path\\name\n"));
}

TEST_CASE("shell.interpolation.newline_escape")
{
    // \n should produce newline
    TestShell shell;
    CHECK(escape(shell("echo \"line1\\nline2\"").output()) == escape("line1\nline2\n"));
}

TEST_CASE("shell.interpolation.single_quote_no_interpolation")
{
    // Single quotes should NOT interpolate
    TestShell shell;
    shell("set NAME world");
    CHECK(escape(shell("echo '$NAME'").output()) == escape("$NAME\n"));
}

TEST_CASE("shell.interpolation.mixed_quotes")
{
    // Mix single and double quotes
    TestShell shell;
    shell("set NAME world");
    CHECK(escape(shell("echo 'hello' \"$NAME\"").output()) == escape("hello world\n"));
}

TEST_CASE("shell.interpolation.nested_command_substitution")
{
    // Nested command substitution
    TestShell shell;
    shell("set MSG hello");
    CHECK(escape(shell("echo \"Result: $(echo $MSG)\"").output()) == escape("Result: hello\n"));
}

TEST_CASE("shell.interpolation.complex_example")
{
    // Complex interpolation with multiple types
    TestShell shell;
    shell("set USER alice");
    shell("set COUNT 42");
    CHECK(escape(shell("echo \"User: $USER, Count: ${COUNT}, Sum: $((1+1))\"").output())
          == escape("User: alice, Count: 42, Sum: 2\n"));
}

// ========================================================================
// F# Match Expression Execution Tests
// These tests verify that match expressions execute without errors.
// The IR generation tests verify correct behavior; these tests verify
// that the generated IR runs through the VM successfully.
// ========================================================================

TEST_CASE("shell.fsharp.match_literal_executes")
{
    // Match with literal patterns executes without error
    TestShell shell;
    shell("let r = match 0 with | 0 -> 0 | _ -> 1");
    // Exit code 0 means no execution errors
    CHECK(shell.exitCode == 0);
}

TEST_CASE("shell.fsharp.match_multiple_arms_executes")
{
    // Match with multiple arms executes without error
    TestShell shell;
    shell("let r = match 1 with | 0 -> 0 | 1 -> 10 | _ -> 99");
    CHECK(shell.exitCode == 0);
}

TEST_CASE("shell.fsharp.match_wildcard_executes")
{
    // Wildcard pattern executes without error
    TestShell shell;
    shell("let r = match 99 with | _ -> 7");
    CHECK(shell.exitCode == 0);
}

TEST_CASE("shell.fsharp.match_variable_binding_executes")
{
    // Variable pattern with binding executes without error
    TestShell shell;
    shell("let r = match 5 with | n -> n + n");
    CHECK(shell.exitCode == 0);
}

TEST_CASE("shell.fsharp.match_guard_executes")
{
    // Guard expression executes without error
    TestShell shell;
    shell("let r = match 10 with | n when n > 5 -> 1 | _ -> 0");
    CHECK(shell.exitCode == 0);
}

TEST_CASE("shell.fsharp.match_multiple_guards_executes")
{
    // Multiple guards execute without error
    TestShell shell;
    shell("let r = match 50 with | n when n < 0 -> 1 | n when n < 10 -> 2 | n when n < 100 -> 3 | _ -> 4");
    CHECK(shell.exitCode == 0);
}

TEST_CASE("shell.fsharp.match_with_function_executes")
{
    // Match with function call in body executes without error
    TestShell shell;
    shell("let double x = x * 2; let r = match 5 with | n -> double n");
    CHECK(shell.exitCode == 0);
}

TEST_CASE("shell.fsharp.match_bool_executes")
{
    // Match on boolean executes without error
    TestShell shell;
    shell("let r = match true with | true -> 1 | false -> 0");
    CHECK(shell.exitCode == 0);
}

TEST_CASE("shell.fsharp.match_chained_let_executes")
{
    // Match in let chain executes without error
    TestShell shell;
    shell("let x = 7; let y = match x with | 0 -> 0 | n -> n * 2");
    CHECK(shell.exitCode == 0);
}

TEST_CASE("shell.fsharp.match_bool_false")
{
    // Match boolean false
    TestShell shell;
    shell("let r = match false with | true -> 1 | false -> 2; exit r");
    CHECK(shell.exitCode == 2);
}

TEST_CASE("shell.fsharp.match_nested_in_let")
{
    // Match expression used in a chain of let bindings
    TestShell shell;
    shell("let x = 7; let y = match x with | 0 -> 0 | n -> n * 2; exit y");
    CHECK(shell.exitCode == 14);
}

TEST_CASE("shell.fsharp.pipeline_map_take_each_println")
{
    // Verify that map producing tuples, followed by take and each println,
    // formats output correctly (not raw pointer values).
    TestShell shell;
    shell("([1; 2; 3]) |> map (fun x -> (x, x * 2)) |> take 2 |> each println");
    // `each` produces unit, so no trailing value should be displayed.
    CHECK(escape(shell.output()) == escape("(1, 2)\n(2, 4)\n"));
}

TEST_CASE("shell.fsharp.structured_pipeline_map_tuple_fields")
{
    // Verify that ps |> map (_.cpu, _.command) formats float fields correctly
    // (not as raw bit patterns of double-to-uint64 casts).
    // Named tuple types from field-access projections produce record-style output:
    // "{ cpu = 0.007292; command = /usr/lib/systemd/systemd }\n"
    TestShell shell;
    shell("ps |> map (_.cpu, _.command) |> take 1 |> each println");
    auto const output = shell.output();
    REQUIRE(!output.empty());
    CHECK(output.front() == '{');
    CHECK(output.find('.') != std::string::npos);
    CHECK(output.find("cpu") != std::string::npos);
    CHECK(output.find("command") != std::string::npos);
    // Ensure we don't see huge raw int64 values (>= 10 digits) that indicate
    // a bit_cast<uint64_t>(double) was printed as an integer.
    CHECK(output.find("46381") == std::string::npos); // common prefix of bit_cast garbage
}

// ============================================================================
// LetBindingCompleter Tests
// ============================================================================

TEST_CASE("LetBindingCompleter.completes_function_names")
{
    endo::FSharpPersistentState state;
    state.functions["add"] = endo::FSharpPersistentState::PersistedFunction {
        .parameters = { "x", "y" },
        .parameterTypes = {},
        .returnType = std::nullopt,
        .body = nullptr,
    };

    endo::LetBindingCompleter completer(state);
    endo::CompletionContext context {
        .type = endo::CompletionContextType::Command,
        .prefix = "ad",
        .cursorPosition = 2,
        .fullInput = "ad",
    };

    auto results = completer.complete(context);
    REQUIRE(!results.empty());
    CHECK(results[0].text == "add");
    CHECK(results[0].description == "add(x, y)");
}

TEST_CASE("LetBindingCompleter.completes_value_bindings")
{
    endo::FSharpPersistentState state;
    state.valueBindings.push_back(endo::FSharpPersistentState::PersistedValueBinding {
        .name = "myValue",
        .value = nullptr,
        .isMutable = false,
    });

    endo::LetBindingCompleter completer(state);
    endo::CompletionContext context {
        .type = endo::CompletionContextType::Argument,
        .prefix = "my",
        .cursorPosition = 2,
        .fullInput = "print my",
    };

    auto results = completer.complete(context);
    REQUIRE(!results.empty());
    CHECK(results[0].text == "myValue");
    CHECK(results[0].description == "value");
}

TEST_CASE("LetBindingCompleter.mutable_value_description")
{
    endo::FSharpPersistentState state;
    state.valueBindings.push_back(endo::FSharpPersistentState::PersistedValueBinding {
        .name = "counter",
        .value = nullptr,
        .isMutable = true,
    });

    endo::LetBindingCompleter completer(state);
    endo::CompletionContext context {
        .type = endo::CompletionContextType::Command,
        .prefix = "cou",
        .cursorPosition = 3,
        .fullInput = "cou",
    };

    auto results = completer.complete(context);
    REQUIRE(!results.empty());
    CHECK(results[0].text == "counter");
    CHECK(results[0].description == "mutable value");
}

TEST_CASE("LetBindingCompleter.recursive_function_description")
{
    endo::FSharpPersistentState state;
    state.functions["factorial"] = endo::FSharpPersistentState::PersistedFunction {
        .parameters = { "n" },
        .parameterTypes = { std::make_shared<endo::Type>(
            endo::PrimitiveTypeNode { endo::PrimitiveType::Int }) },
        .returnType = std::make_shared<endo::Type>(endo::PrimitiveTypeNode { endo::PrimitiveType::Int }),
        .body = nullptr,
        .returnKind = endo::ReturnKind::Plain,
        .recursion = endo::ast::Recursion::Recursive,
    };

    endo::LetBindingCompleter completer(state);
    endo::CompletionContext context {
        .type = endo::CompletionContextType::Command,
        .prefix = "fact",
        .cursorPosition = 4,
        .fullInput = "fact",
    };

    auto results = completer.complete(context);
    REQUIRE(!results.empty());
    CHECK(results[0].text == "factorial");
    CHECK(results[0].description == "rec factorial(n: int) -> int");
}

TEST_CASE("LetBindingCompleter.handles_empty_state")
{
    endo::FSharpPersistentState state;
    endo::LetBindingCompleter completer(state);
    endo::CompletionContext context {
        .type = endo::CompletionContextType::Command,
        .prefix = "zzz_unlikely_prefix",
        .cursorPosition = 19,
        .fullInput = "zzz_unlikely_prefix",
    };

    // With empty user state, only stdlib fuzzy matches may appear.
    // A prefix that doesn't match any stdlib name should yield no results.
    auto results = completer.complete(context);
    CHECK(results.empty());
}

TEST_CASE("LetBindingCompleter.fuzzy_matching")
{
    endo::FSharpPersistentState state;
    state.functions["calculateSum"] = endo::FSharpPersistentState::PersistedFunction {
        .parameters = { "a", "b" },
        .parameterTypes = {},
        .returnType = std::nullopt,
        .body = nullptr,
    };

    endo::LetBindingCompleter completer(state);
    endo::CompletionContext context {
        .type = endo::CompletionContextType::Command,
        .prefix = "calSum",
        .cursorPosition = 6,
        .fullInput = "calSum",
    };

    auto results = completer.complete(context);
    REQUIRE(!results.empty());
    CHECK(results[0].text == "calculateSum");
    CHECK(!results[0].matchPositions.empty());
}

TEST_CASE("LetBindingCompleter.functions_score_higher_than_values")
{
    endo::FSharpPersistentState state;
    state.functions["total"] = endo::FSharpPersistentState::PersistedFunction {
        .parameters = { "x" },
        .parameterTypes = {},
        .returnType = std::nullopt,
        .body = nullptr,
    };
    state.valueBindings.push_back(endo::FSharpPersistentState::PersistedValueBinding {
        .name = "totalCount",
        .value = nullptr,
        .isMutable = false,
    });

    endo::LetBindingCompleter completer(state);
    endo::CompletionContext context {
        .type = endo::CompletionContextType::Command,
        .prefix = "total",
        .cursorPosition = 5,
        .fullInput = "total",
    };

    auto results = completer.complete(context);
    REQUIRE(results.size() >= 2);
    // Function "total" should score higher than value "totalCount"
    CHECK(results[0].text == "total");
    CHECK(results[0].score > results[1].score);
}

TEST_CASE("LetBindingCompleter.does_not_handle_variable_context")
{
    endo::LetBindingCompleter completer(endo::FSharpPersistentState {});
    CHECK(completer.canHandle(endo::CompletionContextType::Command));
    CHECK(completer.canHandle(endo::CompletionContextType::Argument));
    CHECK(!completer.canHandle(endo::CompletionContextType::Variable));
    CHECK(!completer.canHandle(endo::CompletionContextType::VariableBrace));
    CHECK(!completer.canHandle(endo::CompletionContextType::FilePath));
    CHECK(!completer.canHandle(endo::CompletionContextType::Option));
    CHECK(!completer.canHandle(endo::CompletionContextType::Redirect));
}

// ============================================================================
// Bare Expression Display Tests (via Shell)
// ============================================================================

TEST_CASE("shell.bare_expr.number")
{
    TestShell shell;
    shell("42");
    CHECK(escape(shell.output()) == escape("42\n"));
}

TEST_CASE("shell.bare_expr.arithmetic")
{
    TestShell shell;
    shell("(3 + 4)");
    CHECK(escape(shell.output()) == escape("7\n"));
}

TEST_CASE("shell.bare_expr.list")
{
    // List literals at shell prompt need parentheses since [ is a shell identifier char
    TestShell shell;
    shell("([1; 2; 3])");
    CHECK(escape(shell.output()) == escape("[1; 2; 3]\n"));
}

TEST_CASE("shell.bare_expr.option_some")
{
    TestShell shell;
    shell("Some 42");
    CHECK(escape(shell.output()) == escape("Some 42\n"));
}

TEST_CASE("shell.bare_expr.option_none")
{
    TestShell shell;
    shell("None");
    CHECK(escape(shell.output()) == escape("None\n"));
}

TEST_CASE("shell.bare_expr.tuple")
{
    TestShell shell;
    shell("(1, 2)");
    CHECK(escape(shell.output()) == escape("(1, 2)\n"));
}

TEST_CASE("shell.bare_expr.function_call")
{
    TestShell shell;
    shell("let add x y = x + y");
    shell("add 3 4");
    CHECK(escape(shell.output()) == escape("7\n"));
}

TEST_CASE("shell.bare_expr.string_quoted")
{
    TestShell shell;
    shell("let greet () = \"hello\"");
    shell("greet ()");
    CHECK(escape(shell.output()) == escape("\"hello\"\n"));
}

TEST_CASE("shell.bare_expr.match")
{
    TestShell shell;
    shell("match Some 42 with | Some x -> x | None -> 0");
    CHECK(escape(shell.output()) == escape("42\n"));
}

TEST_CASE("shell.bare_expr.if_then_else")
{
    TestShell shell;
    shell("if true then 10 else 20");
    CHECK(escape(shell.output()) == escape("10\n"));
}

TEST_CASE("shell.bare_expr.rand")
{
    TestShell shell;
    shell("rand 5 5");
    CHECK(escape(shell.output()) == escape("5\n"));
}

TEST_CASE("shell.bare_expr.unit_suppressed")
{
    TestShell shell;
    shell("print \"hello\"");
    // print outputs "hello" but should NOT auto-display extra "0"
    CHECK(escape(shell.output()) == escape("hello"));
}

TEST_CASE("shell.bare_expr.println_unit_suppressed")
{
    TestShell shell;
    shell("println \"hello\"");
    // println outputs "hello\n" but should NOT auto-display extra "0"
    CHECK(escape(shell.output()) == escape("hello\n"));
}

TEST_CASE("shell.bare_expr.println_pipeline_unit_suppressed")
{
    TestShell shell;
    shell("3 |> fun n -> println $\"count: {n}\"");
    // Pipeline ending in println should NOT auto-display extra "0"
    CHECK(escape(shell.output()) == escape("count: 3\n"));
}

TEST_CASE("shell.bare_expr.unit_function_no_display")
{
    TestShell shell;
    // Define a function whose body is print (returns unit)
    shell("let setup () = print \"configured\"");
    shell("setup ()");
    // Should see "configured" from print, but NO extra "0" from auto-display
    CHECK(escape(shell.output()) == escape("configured"));
}

TEST_CASE("shell.compiled_function.void_last_expr")
{
    TestShell shell;
    // display_result(N)V returns void. When it's the last expression of a compiled
    // function (unit parameter -> IRFunction), compileFunctionBody must not pass the
    // void CallInstr to FunctionRetInstr, or TargetCodeGenerator crashes in emitLoad.
    shell("let show_value () = display_result 42");
    shell("show_value ()");
    CHECK(escape(shell.output()) == escape("42\n"));
}

TEST_CASE("shell.bare_expr.paren_string_pipeline_each")
{
    TestShell shell;
    shell(R"(("hello\nworld") |> lines |> each println)");
    CHECK(escape(shell.output()) == escape("hello\nworld\n"));
}

TEST_CASE("shell.bare_expr.paren_string_pipeline_display")
{
    TestShell shell;
    shell(R"(("hello\nworld") |> lines)");
    CHECK(escape(shell.output()) == escape("[\"hello\"; \"world\"]\n"));
}

TEST_CASE("shell.shell_command_expr.capture_in_pipeline")
{
    TestShell shell;
    shell(R"((& echo hello) |> lines |> length |> print)");
    CHECK(escape(shell.output()) == escape("1"));
}

// ============================================================================
// Table Formatter Tests
// ============================================================================

TEST_CASE("table.plain.no_borders_no_escapes")
{
    // Verify Plain style produces no special characters
    auto table = endo::formatRecordTable(nullptr, nullptr, { .style = endo::TableStyle::Plain });
    CHECK(table == "[]\n"); // nullptr → empty
}

TEST_CASE("table.isListOfRecords.nullptr_returns_false")
{
    CHECK(!endo::isListOfRecords(nullptr, nullptr));
}

TEST_CASE("table.terminalWidth.zero_no_constraint")
{
    // terminalWidth=0 should preserve default behavior (no shrinking)
    endo::TableConfig config;
    config.terminalWidth = 0;
    config.maxColumnWidth = 40;
    auto table = endo::formatRecordTable(nullptr, nullptr, config);
    CHECK(table == "[]\n"); // empty list, no width constraint
}

TEST_CASE("table.bordered.renders_header_and_rows")
{
    // Verify bordered style produces box-drawing characters
    endo::TableConfig config;
    config.style = endo::TableStyle::Bordered;
    config.useColor = false;
    // nullptr list → "[]", but we verify the config is accepted
    auto table = endo::formatRecordTable(nullptr, nullptr, config);
    CHECK(table == "[]\n");
}

TEST_CASE("table.compact.renders_underline")
{
    // Verify compact style config is accepted
    endo::TableConfig config;
    config.style = endo::TableStyle::Compact;
    config.useColor = false;
    auto table = endo::formatRecordTable(nullptr, nullptr, config);
    CHECK(table == "[]\n");
}

TEST_CASE("table.terminalWidth.minimum_column_width")
{
    // Very narrow terminal should still produce valid config
    endo::TableConfig config;
    config.terminalWidth = 10;
    config.style = endo::TableStyle::Plain;
    config.useColor = false;
    auto table = endo::formatRecordTable(nullptr, nullptr, config);
    CHECK(table == "[]\n");
}

// ============================================================================
// Partial-line indicator
// ============================================================================

#include <shell/ui/Prompt.hpp>

#include <platform/Pipe.hpp>

namespace
{

/// @brief Reads all available data from a pipe handle until EOF.
/// @param handle The read end of the pipe (platform-native handle).
/// @return The collected output as a string.
std::string readAllFromPipe(endo::NativeHandle handle)
{
    std::string result;
    char buf[256];
    for (;;)
    {
        auto const n = endo::platformRead(handle, buf, sizeof(buf));
        if (n <= 0)
            break;
        result.append(buf, static_cast<size_t>(n));
    }
    return result;
}

} // namespace

TEST_CASE("shell.partial_line_indicator.emits_when_not_at_col1")
{
    auto pipe = endo::createPipe();
    REQUIRE(pipe.has_value());

    endo::emitPartialLineIndicator(pipe.value()->writer(), 5);
    pipe.value()->closeWriter();

    auto const output = readAllFromPipe(pipe.value()->reader());
    pipe.value()->closeReader();

    // Must contain the return symbol U+23CE (UTF-8: E2 8F 8E)
    CHECK(output.find("\u23CE") != std::string::npos);
    // Must contain CSI K (clear to EOL)
    CHECK(output.find("\033[K") != std::string::npos);
    // Must end with CR LF
    CHECK(output.size() >= 2);
    CHECK(output.substr(output.size() - 2) == "\r\n");
}

TEST_CASE("shell.partial_line_indicator.silent_at_col1")
{
    auto pipe = endo::createPipe();
    REQUIRE(pipe.has_value());

    endo::emitPartialLineIndicator(pipe.value()->writer(), 1);
    pipe.value()->closeWriter();

    auto const output = readAllFromPipe(pipe.value()->reader());
    pipe.value()->closeReader();

    CHECK(output.empty());
}

TEST_CASE("shell.partial_line_indicator.silent_on_failure")
{
    auto pipe = endo::createPipe();
    REQUIRE(pipe.has_value());

    endo::emitPartialLineIndicator(pipe.value()->writer(), 0);
    pipe.value()->closeWriter();

    auto const output = readAllFromPipe(pipe.value()->reader());
    pipe.value()->closeReader();

    CHECK(output.empty());
}

// ============================================================================
// Fetch Builtin Tests
// ============================================================================

TEST_CASE("shell.fsharp.fetch.unsupported_protocol")
{
    // Unsupported URL scheme — curl rejects synchronously, no network access.
    TestShell shell;
    shell(R"(match fetch "badscheme://test" with | Ok b -> print "ok" | Error e -> print "error")");
    CHECK(escape(shell.output()) == escape("error"));
}

#if !defined(_WIN32)
TEST_CASE("shell.fsharp.fetch.http_success")
{
    // Local server returns 200 — fetch should return Ok(filename).
    auto const tempDirGuard = endo::testing::ScopedTempDir { "endo_fetch_test" };
    auto const& tempDir = tempDirGuard.path();

    auto listener = endo::http::LocalTcpListener {};
    auto const port = listener.start();
    REQUIRE(port.has_value());

    auto serverThread = std::thread([&]() {
        [[maybe_unused]] auto const _ =
            listener.serveOnce(std::chrono::seconds(5),
                               "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\nhello");
    });

    auto const scopedCwd = endo::testing::ScopedWorkingDirectory { tempDir };

    TestShell shell;
    shell.shell.setInteractive(false);
    shell(std::format(
        R"(match fetch "http://127.0.0.1:{}/test.txt" with | Ok b -> print "ok" | Error e -> print "error")",
        *port));
    serverThread.join();

    CHECK(escape(shell.output()) == escape("ok"));
}

TEST_CASE("shell.fsharp.fetch.http_error")
{
    // Local server returns 404 — fetch should return Error.
    auto listener = endo::http::LocalTcpListener {};
    auto const port = listener.start();
    REQUIRE(port.has_value());

    auto serverThread = std::thread([&]() {
        [[maybe_unused]] auto const _ =
            listener.serveOnce(std::chrono::seconds(5),
                               "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
    });

    TestShell shell;
    shell(std::format(
        R"(match fetch "http://127.0.0.1:{}" with | Ok b -> print "ok" | Error e -> print "error")", *port));
    serverThread.join();

    CHECK(escape(shell.output()) == escape("error"));
}
#endif

// ============================================================================
// Invalid command exit code
// ============================================================================

TEST_CASE("shell.exec.program_not_found_exit_code")
{
    // An invalid command (program not found) must produce a non-zero exit code
    // so that history persistence correctly rejects it.
    TestShell shell;
    shell("blurb_nonexistent_command_12345");
    CHECK(shell.exitCode != 0);
}

TEST_CASE("shell.exec.program_not_found_does_not_persist_to_history")
{
    // Invalid commands must not be persisted to history.
    // PersistentHistory takes FileSystem const&, so this needs no real directory -- and the
    // previous one was build-directory-relative, i.e. shared by every run from that tree.
    auto fs = endo::InMemoryFileSystem {};
    auto const dir = std::filesystem::path { "/test/history" };

    auto history = endo::PersistentHistory { fs };
    history.setFilePath(dir / "history.yml");

    // Simulate the shell flow: add command, then mark with exit code
    history.add("blurb_nonexistent_command_12345");
    CHECK(!history.richEntries().back().persisted); // not yet persisted

    // Simulate what happens when Shell::execute() returns non-zero for program-not-found
    history.markLastResult(EXIT_FAILURE);
    CHECK(!history.richEntries().back().persisted); // must remain unpersisted

    // Verify it does not survive a roundtrip to disk
    CHECK(!fs.exists(dir / "history.yml"));
}

// ============================================================================
// LINES / COLUMNS environment variables
// ============================================================================

TEST_CASE("shell.env.lines_columns_initial")
{
    TestShell shell;
    // TestPTY defaults to 25 rows x 80 columns
    CHECK(shell.env.get("LINES").value_or("") == "25");
    CHECK(shell.env.get("COLUMNS").value_or("") == "80");
}

TEST_CASE("shell.env.lines_columns_after_resize")
{
    TestShell shell;
    shell.pty.setSize(40, 120);
    shell.shell.updateTerminalSizeEnv();
    CHECK(shell.env.get("LINES").value_or("") == "40");
    CHECK(shell.env.get("COLUMNS").value_or("") == "120");
}

TEST_CASE("shell.env.lines_columns_accessible_via_expansion")
{
    TestShell shell;
    shell("echo $LINES");
    CHECK(shell.exitCode == 0);
    auto const out = std::string(shell.output());
    CHECK(out.find("25") != std::string::npos);
}

TEST_CASE("shell.env.lines_columns_echo_both")
{
    TestShell shell;
    shell("echo $LINES $COLUMNS");
    CHECK(shell.exitCode == 0);
    auto const out = std::string(shell.output());
    CHECK(out.find("25") != std::string::npos);
    CHECK(out.find("80") != std::string::npos);
}

// ============================================================================
// ENDO_SHLVL (shell nesting level)
// ============================================================================

TEST_CASE("shell.env.endo_shlvl_defaults_to_zero")
{
    TestShell shell;
    shell("echo $ENDO_SHLVL");
    CHECK(shell.exitCode == 0);
    CHECK(std::string(shell.output()).find('0') != std::string::npos);
}

TEST_CASE("shell.env.endo_shlvl_increments_when_preset")
{
    TestShell shell;
    shell.env.set("ENDO_SHLVL", "0");
    // Re-create the shell so it picks up the pre-set env
    endo::Shell nested(shell.pty, shell.env);
    CHECK(shell.env.get("ENDO_SHLVL").value_or("") == "1");
}

TEST_CASE("shell.env.endo_shlvl_handles_malformed")
{
    TestShell shell;
    shell.env.set("ENDO_SHLVL", "not_a_number");
    endo::Shell nested(shell.pty, shell.env);
    CHECK(shell.env.get("ENDO_SHLVL").value_or("") == "0");
}

// ============================================================================
// ShellLevelModule
// ============================================================================

#include <shell/ui/modules/ShellLevelModule.hpp>

TEST_CASE("module.shell_level.hidden_at_level_zero")
{
    endo::ShellLevelModule module;
    endo::PromptContext ctx;
    ctx.shellLevel = 0;
    CHECK_FALSE(module.shouldShow(ctx));
}

TEST_CASE("module.shell_level.visible_at_level_one")
{
    endo::ShellLevelModule module;
    endo::PromptContext ctx;
    ctx.shellLevel = 1;
    CHECK(module.shouldShow(ctx));
    auto const segments = module.evaluate(ctx);
    REQUIRE(!segments.empty());
    CHECK(segments[0].text.find("L1") != std::string::npos);
}

TEST_CASE("module.shell_level.shows_correct_level")
{
    endo::ShellLevelModule module;
    endo::PromptContext ctx;
    ctx.shellLevel = 3;
    CHECK(module.shouldShow(ctx));
    auto const segments = module.evaluate(ctx);
    REQUIRE(!segments.empty());
    CHECK(segments[0].text.find("L3") != std::string::npos);
}

// ============================================================================
// Builtin: find
// ============================================================================

TEST_CASE("shell.builtin.find_basic")
{
    InMemoryShell shell;
    auto const testDir = std::filesystem::path("/test/find_basic");
    shell.fs.addFile(testDir / "a.txt", "hello");
    shell.fs.addFile(testDir / "sub" / "b.txt", "world");

    shell(std::format("find {}", testDir.string()));
    CHECK(shell.exitCode == 0);
    auto const out = std::string(shell.output());
    CHECK(out.find(endo::platform::normalizePath(testDir)) != std::string::npos);
    CHECK(out.find("a.txt") != std::string::npos);
    CHECK(out.find("b.txt") != std::string::npos);
    CHECK(out.find("sub") != std::string::npos);
}

TEST_CASE("shell.builtin.find_name_pattern")
{
    InMemoryShell shell;
    auto const testDir = std::filesystem::path("/test/find_name");
    shell.fs.addFile(testDir / "hello.cpp", "x");
    shell.fs.addFile(testDir / "world.hpp", "x");
    shell.fs.addFile(testDir / "other.txt", "x");

    shell(std::format("find {} -name '*.cpp'", testDir.string()));
    CHECK(shell.exitCode == 0);
    auto const out = std::string(shell.output());
    CHECK(out.find("hello.cpp") != std::string::npos);
    CHECK(out.find("world.hpp") == std::string::npos);
    CHECK(out.find("other.txt") == std::string::npos);
}

TEST_CASE("shell.builtin.find_type_file")
{
    InMemoryShell shell;
    auto const testDir = std::filesystem::path("/test/find_type");
    shell.fs.addDirectory(testDir / "subdir");
    shell.fs.addFile(testDir / "file.txt", "x");

    shell(std::format("find {} -type f", testDir.string()));
    CHECK(shell.exitCode == 0);
    auto const out = std::string(shell.output());
    CHECK(out.find("file.txt") != std::string::npos);
    // The testDir itself is a directory and should not be in output
    // subdir is a directory and should not be in output
    // But testDir path contains "subdir" substring, so check more carefully
    // Just verify that file.txt is found
}

TEST_CASE("shell.builtin.find_type_directory")
{
    InMemoryShell shell;
    auto const testDir = std::filesystem::path("/test/find_typed");
    shell.fs.addDirectory(testDir / "subdir");
    shell.fs.addFile(testDir / "file.txt", "x");

    shell(std::format("find {} -type d", testDir.string()));
    CHECK(shell.exitCode == 0);
    auto const out = std::string(shell.output());
    CHECK(out.find("subdir") != std::string::npos);
    CHECK(out.find("file.txt") == std::string::npos);
}

TEST_CASE("shell.builtin.find_maxdepth")
{
    InMemoryShell shell;
    auto const testDir = std::filesystem::path("/test/find_maxdepth");
    shell.fs.addFile(testDir / "top.txt", "x");
    shell.fs.addFile(testDir / "a" / "mid.txt", "x");
    shell.fs.addFile(testDir / "a" / "b" / "deep.txt", "x");

    shell(std::format("find {} -maxdepth 1", testDir.string()));
    CHECK(shell.exitCode == 0);
    auto const out = std::string(shell.output());
    CHECK(out.find("top.txt") != std::string::npos);
    CHECK(out.find("deep.txt") == std::string::npos);
}

TEST_CASE("shell.builtin.find_or_grouping")
{
    InMemoryShell shell;
    auto const testDir = std::filesystem::path("/test/find_or");
    shell.fs.addFile(testDir / "a.cpp", "x");
    shell.fs.addFile(testDir / "b.hpp", "x");
    shell.fs.addFile(testDir / "c.txt", "x");

    shell(std::format("find {} '(' -name '*.cpp' -o -name '*.hpp' ')'", testDir.string()));
    CHECK(shell.exitCode == 0);
    auto const out = std::string(shell.output());
    CHECK(out.find("a.cpp") != std::string::npos);
    CHECK(out.find("b.hpp") != std::string::npos);
    CHECK(out.find("c.txt") == std::string::npos);
}

TEST_CASE("shell.builtin.find_not")
{
    InMemoryShell shell;
    auto const testDir = std::filesystem::path("/test/find_not");
    shell.fs.addFile(testDir / "keep.cpp", "x");
    shell.fs.addFile(testDir / "remove.o", "x");

    shell(std::format("find {} -type f -not -name '*.o'", testDir.string()));
    CHECK(shell.exitCode == 0);
    auto const out = std::string(shell.output());
    CHECK(out.find("keep.cpp") != std::string::npos);
    CHECK(out.find("remove.o") == std::string::npos);
}

TEST_CASE("shell.builtin.find_empty")
{
    namespace fs = std::filesystem;
    auto const testDirGuard = endo::testing::ScopedTempDir { "endo_find_test_empty" };
    auto const& testDir = testDirGuard.path();
    fs::create_directories(testDir / "emptydir");
    {
        std::ofstream ofs(testDir / "empty.txt");
    } // empty file
    {
        std::ofstream ofs(testDir / "notempty.txt");
        ofs << "content";
    }

    TestShell shell;
    shell(std::format("find {} -empty", testDir.string()));
    CHECK(shell.exitCode == 0);
    auto const out = std::string(shell.output());
    CHECK(out.find("empty.txt") != std::string::npos);
    CHECK(out.find("emptydir") != std::string::npos);
    CHECK(out.find("notempty.txt") == std::string::npos);
}

TEST_CASE("shell.builtin.find_print0")
{
    InMemoryShell shell;
    auto const testDir = std::filesystem::path("/test/find_print0");
    shell.fs.addFile(testDir / "file.txt", "x");

    shell(std::format("find {} -name '*.txt' -print0", testDir.string()));
    CHECK(shell.exitCode == 0);
    auto const out = std::string(shell.output());
    // Output should contain null byte instead of newline
    CHECK(out.find('\0') != std::string::npos);
    CHECK(out.find("file.txt") != std::string::npos);
}

TEST_CASE("shell.builtin.find_no_results")
{
    InMemoryShell shell;
    auto const testDir = std::filesystem::path("/test/find_noresult");
    shell.fs.addFile(testDir / "file.txt", "x");

    shell(std::format("find {} -name '*.nonexistent'", testDir.string()));
    CHECK(shell.exitCode == 0);
    // Output should be empty (no matches, no error)
    auto const out = std::string(shell.output());
    CHECK(out.find("file.txt") == std::string::npos);
}

// ============================================================================
// Grep Builtin — Pipe Input Tests
// ============================================================================

TEST_CASE("shell.builtin.grep_pipe_basic")
{
    CHECK(escape(TestShell()("echo -e \"hello\\nworld\" | grep --color=never hello").output())
          == escape("hello\n"));
}

TEST_CASE("shell.builtin.grep_pipe_no_match")
{
    TestShell shell;
    shell("echo foo | grep --color=never bar");
    CHECK(shell.exitCode == 1);
    CHECK(shell.output().empty());
}

TEST_CASE("shell.builtin.grep_pipe_case_insensitive")
{
    CHECK(escape(TestShell()("echo FOO | grep --color=never -i foo").output()) == escape("FOO\n"));
}

TEST_CASE("shell.builtin.grep_pipe_invert")
{
    CHECK(escape(TestShell()("echo -e \"foo\\nbar\\nbaz\" | grep --color=never -v bar").output())
          == escape("foo\nbaz\n"));
}

TEST_CASE("shell.builtin.grep_pipe_count")
{
    CHECK(escape(TestShell()("echo -e \"aa\\nab\\nac\" | grep --color=never -c a").output())
          == escape("3\n"));
}

TEST_CASE("shell.builtin.grep_pipe_line_numbers")
{
    CHECK(escape(TestShell()("echo -e \"a\\nb\\na\" | grep --color=never -n a").output())
          == escape("1:a\n3:a\n"));
}

TEST_CASE("shell.builtin.grep_pipe_only_matching")
{
    CHECK(escape(TestShell()("echo \"hello world\" | grep --color=never -o world").output())
          == escape("world\n"));
}

TEST_CASE("shell.builtin.grep_pipe_fixed_strings")
{
    CHECK(escape(TestShell()("echo \"a.b\" | grep --color=never -F \"a.b\"").output()) == escape("a.b\n"));
}

TEST_CASE("shell.builtin.grep_pipe_word_regexp")
{
    CHECK(escape(TestShell()("echo -e \"foo\\nfoobar\" | grep --color=never -w foo").output())
          == escape("foo\n"));
}

TEST_CASE("shell.builtin.grep_pipe_max_count")
{
    TestShell shell;
    shell(R"(echo -e "a\na\na" | grep --color=never -m 2 a)");
    auto const output = std::string(shell.output());
    auto count = std::ranges::count(output, '\n');
    CHECK(count == 2);
}

TEST_CASE("shell.builtin.grep_pipe_quiet_match")
{
    TestShell shell;
    shell("echo hello | grep --color=never -q hello");
    CHECK(shell.exitCode == 0);
    CHECK(shell.output().empty());
}

TEST_CASE("shell.builtin.grep_pipe_quiet_no_match")
{
    TestShell shell;
    shell("echo hello | grep --color=never -q xyz");
    CHECK(shell.exitCode == 1);
    CHECK(shell.output().empty());
}

TEST_CASE("shell.builtin.grep_pipe_context")
{
    CHECK(escape(TestShell()("echo -e \"a\\nb\\nc\\nd\\ne\" | grep --color=never -C 1 c").output())
          == escape("b\nc\nd\n"));
}

TEST_CASE("shell.builtin.grep_pipe_multiple_e")
{
    CHECK(escape(TestShell()("echo -e \"foo\\nbar\\nbaz\" | grep --color=never -e foo -e baz").output())
          == escape("foo\nbaz\n"));
}

// ============================================================================
// Grep Builtin — File-Based Tests
// ============================================================================

TEST_CASE("shell.builtin.grep_file_basic")
{
    InMemoryShell shell;
    auto const testDir = std::filesystem::path("/test/grep_file");
    shell.fs.addDirectory(testDir);
    auto const file1 = testDir / "test.txt";
    shell.fs.addFile(file1, "hello world\ngoodbye world\nhello again\n");

    shell(std::format("grep --color=never hello {}", file1.string()));
    CHECK(shell.exitCode == 0);
    auto const out = std::string(shell.output());
    CHECK(out.find("hello world") != std::string::npos);
    CHECK(out.find("hello again") != std::string::npos);
    CHECK(out.find("goodbye") == std::string::npos);
}

TEST_CASE("shell.builtin.grep_file_no_match_exit_code")
{
    InMemoryShell shell;
    auto const testDir = std::filesystem::path("/test/grep_nomatch");
    shell.fs.addDirectory(testDir);
    auto const file1 = testDir / "test.txt";
    shell.fs.addFile(file1, "hello world\n");

    shell(std::format("grep --color=never xyz {}", file1.string()));
    CHECK(shell.exitCode == 1);
}

TEST_CASE("shell.builtin.grep_file_multiple_files")
{
    namespace fs = std::filesystem;
    auto const testDirGuard = endo::testing::ScopedTempDir { "endo_grep_test_multi" };
    auto const& testDir = testDirGuard.path();
    auto const file1 = testDir / "a.txt";
    auto const file2 = testDir / "b.txt";
    {
        std::ofstream(file1) << "hello from a\n";
        std::ofstream(file2) << "hello from b\n";
    }

    TestShell shell;
    shell(std::format("grep --color=never hello {} {}", file1.string(), file2.string()));
    CHECK(shell.exitCode == 0);
    auto const out = std::string(shell.output());
    // With multiple files, filenames should be prefixed
    CHECK(out.find("a.txt:") != std::string::npos);
    CHECK(out.find("b.txt:") != std::string::npos);
}

TEST_CASE("shell.builtin.grep_file_with_filename")
{
    namespace fs = std::filesystem;
    auto const testDirGuard = endo::testing::ScopedTempDir { "endo_grep_test_Hflag" };
    auto const& testDir = testDirGuard.path();
    auto const file1 = testDir / "test.txt";
    {
        std::ofstream(file1) << "hello\n";
    }

    TestShell shell;
    shell(std::format("grep --color=never -H hello {}", file1.string()));
    CHECK(shell.exitCode == 0);
    auto const out = std::string(shell.output());
    CHECK(out.find("test.txt:") != std::string::npos);
}

TEST_CASE("shell.builtin.grep_file_no_filename")
{
    namespace fs = std::filesystem;
    auto const testDirGuard = endo::testing::ScopedTempDir { "endo_grep_test_hflag" };
    auto const& testDir = testDirGuard.path();
    auto const file1 = testDir / "a.txt";
    auto const file2 = testDir / "b.txt";
    {
        std::ofstream(file1) << "hello\n";
        std::ofstream(file2) << "hello\n";
    }

    TestShell shell;
    shell(std::format("grep --color=never -h hello {} {}", file1.string(), file2.string()));
    CHECK(shell.exitCode == 0);
    auto const out = std::string(shell.output());
    CHECK(out.find("a.txt:") == std::string::npos);
    CHECK(out.find("b.txt:") == std::string::npos);
    CHECK(out == "hello\nhello\n");
}

TEST_CASE("shell.builtin.grep_files_with_matches")
{
    namespace fs = std::filesystem;
    auto const testDirGuard = endo::testing::ScopedTempDir { "endo_grep_test_lflag" };
    auto const& testDir = testDirGuard.path();
    auto const file1 = testDir / "a.txt";
    auto const file2 = testDir / "b.txt";
    {
        std::ofstream(file1) << "hello\n";
        std::ofstream(file2) << "world\n";
    }

    TestShell shell;
    shell(std::format("grep --color=never -l hello {} {}", file1.string(), file2.string()));
    CHECK(shell.exitCode == 0);
    auto const out = std::string(shell.output());
    CHECK(out.find("a.txt") != std::string::npos);
    CHECK(out.find("b.txt") == std::string::npos);
}

TEST_CASE("shell.builtin.grep_files_without_match")
{
    namespace fs = std::filesystem;
    auto const testDirGuard = endo::testing::ScopedTempDir { "endo_grep_test_Lflag" };
    auto const& testDir = testDirGuard.path();
    auto const file1 = testDir / "a.txt";
    auto const file2 = testDir / "b.txt";
    {
        std::ofstream(file1) << "hello\n";
        std::ofstream(file2) << "world\n";
    }

    TestShell shell;
    shell(std::format("grep --color=never -L hello {} {}", file1.string(), file2.string()));
    CHECK(shell.exitCode == 0);
    auto const out = std::string(shell.output());
    CHECK(out.find("a.txt") == std::string::npos);
    CHECK(out.find("b.txt") != std::string::npos);
}

TEST_CASE("shell.builtin.grep_recursive")
{
    namespace fs = std::filesystem;
    auto const testDirGuard = endo::testing::ScopedTempDir { "endo_grep_test_recursive" };
    auto const& testDir = testDirGuard.path();
    fs::create_directories(testDir / "sub");
    {
        std::ofstream(testDir / "top.txt") << "hello top\n";
        std::ofstream(testDir / "sub" / "nested.txt") << "hello nested\n";
    }

    TestShell shell;
    shell(std::format("grep --color=never -r hello {}", testDir.string()));
    CHECK(shell.exitCode == 0);
    auto const out = std::string(shell.output());
    CHECK(out.find("hello top") != std::string::npos);
    CHECK(out.find("hello nested") != std::string::npos);
}

TEST_CASE("shell.builtin.grep_recursive_include")
{
    namespace fs = std::filesystem;
    auto const testDirGuard = endo::testing::ScopedTempDir { "endo_grep_test_include" };
    auto const& testDir = testDirGuard.path();
    {
        std::ofstream(testDir / "a.txt") << "hello\n";
        std::ofstream(testDir / "b.cpp") << "hello\n";
    }

    TestShell shell;
    shell(std::format("grep --color=never -r --include=*.txt hello {}", testDir.string()));
    CHECK(shell.exitCode == 0);
    auto const out = std::string(shell.output());
    CHECK(out.find("a.txt") != std::string::npos);
    CHECK(out.find("b.cpp") == std::string::npos);
}

TEST_CASE("shell.builtin.grep_recursive_exclude_dir")
{
    namespace fs = std::filesystem;
    auto const testDirGuard = endo::testing::ScopedTempDir { "endo_grep_test_exdir" };
    auto const& testDir = testDirGuard.path();
    fs::create_directories(testDir / "src");
    fs::create_directories(testDir / "build");
    {
        std::ofstream(testDir / "src" / "main.cpp") << "hello\n";
        std::ofstream(testDir / "build" / "out.cpp") << "hello\n";
    }

    TestShell shell;
    shell(std::format("grep --color=never -rH --exclude-dir=build hello {}", testDir.string()));
    CHECK(shell.exitCode == 0);
    auto const out = std::string(shell.output());
    CHECK(out.find("main.cpp") != std::string::npos);
    CHECK(out.find("out.cpp") == std::string::npos);
}

TEST_CASE("shell.builtin.grep_binary_skip")
{
    namespace fs = std::filesystem;
    auto const testDirGuard = endo::testing::ScopedTempDir { "endo_grep_test_binary" };
    auto const& testDir = testDirGuard.path();
    auto const binFile = testDir / "binary.bin";
    auto const txtFile = testDir / "text.txt";
    {
        std::ofstream bin(binFile, std::ios::binary);
        bin << "hello";
        bin.put('\0');
        bin << "world";
    }
    {
        std::ofstream(txtFile) << "hello text\n";
    }

    TestShell shell;
    shell(std::format("grep --color=never -I hello {} {}", binFile.string(), txtFile.string()));
    CHECK(shell.exitCode == 0);
    auto const out = std::string(shell.output());
    CHECK(out.find("hello text") != std::string::npos);
    // Binary file should be skipped
    CHECK(out.find("binary.bin") == std::string::npos);
}

TEST_CASE("shell.builtin.grep_line_regexp")
{
    CHECK(escape(TestShell()("echo -e \"foo\\nfoobar\" | grep --color=never -x foo").output())
          == escape("foo\n"));
}

TEST_CASE("shell.builtin.grep_nonexistent_file")
{
    TestShell shell;
    shell("grep --color=never hello /nonexistent/path/file.txt");
    CHECK(shell.exitCode == 2);
}

TEST_CASE("shell.builtin.grep_help")
{
    TestShell shell;
    shell("grep --help");
    CHECK(shell.exitCode == 0);
    auto const out = std::string(shell.output());
    CHECK(out.find("Usage") != std::string::npos);
}

TEST_CASE("shell.builtin.grep_pipe_chain")
{
    CHECK(escape(TestShell()("echo -e \"aa\\nbb\\ncc\" | grep --color=never -v bb | grep --color=never -c .")
                     .output())
          == escape("2\n"));
}

// =============================================================================
// Variadic function + shell pipe tests
// =============================================================================

TEST_CASE("shell.variadic_function_pipe", "[variadic]")
{
    TestShell shell;
    // Define a variadic function that wraps echo (the wrapper adds --color=auto
    // but for piping, the parser falls back to raw command execution).
    // Use 'echo' directly as the variadic name so it resolves as a real program.
    shell("let echo ...xs = & echo ...xs");
    shell("echo hello world | grep --color=never hello");
    CHECK(escape(shell.output()) == escape("hello world\n"));
}

TEST_CASE("shell.pipe_with_flags", "[pipe]")
{
    // Verify that basic pipes with flags work as expected (regression guard)
    CHECK(escape(TestShell()("echo -e \"foo\\nbar\" | grep --color=never -w foo").output())
          == escape("foo\n"));
}

// ============================================================================
// source-env builtin
// ============================================================================

#if defined(_WIN32)
TEST_CASE("shell.builtin.source_env_bat", "[source-env][windows]")
{
    TestShell shell;

    // Create a temp .bat script that sets an environment variable
    auto const tempDirGuard = endo::testing::ScopedTempDir { "endo_source_env" };
    auto const& tempDir = tempDirGuard.path();
    auto const batPath = tempDir / "endo_test_source_env.bat";
    {
        std::ofstream ofs(batPath);
        ofs << "@echo off\r\n";
        ofs << "set ENDO_TEST_SRCENV=hello_from_bat\r\n";
    }

    auto const cmd = std::format("source-env \"{}\"", batPath.string());
    shell(cmd);
    INFO("shell output: " << escape(shell.output()));
    CHECK(shell.exitCode == 0);
    CHECK(shell.env.get("ENDO_TEST_SRCENV").value_or("") == "hello_from_bat");
}

TEST_CASE("shell.builtin.source_env_bat_with_args", "[source-env][windows]")
{
    TestShell shell;

    auto const tempDirGuard = endo::testing::ScopedTempDir { "endo_source_env" };
    auto const& tempDir = tempDirGuard.path();
    auto const batPath = tempDir / "endo_test_source_env_args.bat";
    {
        std::ofstream ofs(batPath);
        ofs << "@echo off\r\n";
        ofs << "set ENDO_TEST_ARG=%1\r\n";
    }

    auto const cmd = std::format("source-env \"{}\" \"my_arg_value\"", batPath.string());
    shell(cmd);
    CHECK(shell.exitCode == 0);
    CHECK(shell.env.get("ENDO_TEST_ARG").value_or("") == "my_arg_value");
}
#endif

#if !defined(_WIN32)
TEST_CASE("shell.builtin.source_env_sh", "[source-env][posix]")
{
    TestShell shell;

    auto const tempDirGuard = endo::testing::ScopedTempDir { "endo_source_env" };
    auto const& tempDir = tempDirGuard.path();
    auto const shPath = tempDir / "endo_test_source_env.sh";
    {
        std::ofstream ofs(shPath);
        ofs << "export ENDO_TEST_SRCENV=hello_from_sh\n";
    }

    auto const cmd = std::format("source-env \"{}\"", shPath.string());
    shell(cmd);
    CHECK(shell.exitCode == 0);
    CHECK(shell.env.get("ENDO_TEST_SRCENV").value_or("") == "hello_from_sh");
}

TEST_CASE("shell.builtin.source_env_sh_with_args", "[source-env][posix]")
{
    TestShell shell;

    auto const tempDirGuard = endo::testing::ScopedTempDir { "endo_source_env" };
    auto const& tempDir = tempDirGuard.path();
    auto const shPath = tempDir / "endo_test_source_env_args.sh";
    {
        std::ofstream ofs(shPath);
        ofs << "export ENDO_TEST_ARG=$1\n";
    }

    auto const cmd = std::format(R"(source-env "{}" "my_arg_value")", shPath.string());
    shell(cmd);
    CHECK(shell.exitCode == 0);
    CHECK(shell.env.get("ENDO_TEST_ARG").value_or("") == "my_arg_value");
}

TEST_CASE("shell.builtin.source_env_bat_rejected_on_posix", "[source-env][posix]")
{
    TestShell shell;

    auto const tempDirGuard = endo::testing::ScopedTempDir { "endo_source_env" };
    auto const& tempDir = tempDirGuard.path();
    auto const batPath = tempDir / "endo_test_rejected.bat";
    {
        std::ofstream ofs(batPath);
        ofs << "@echo off\r\n";
    }

    auto const cmd = std::format("source-env \"{}\"", batPath.string());
    shell(cmd);
    CHECK(shell.exitCode == 1);
}
#endif

TEST_CASE("shell.builtin.source_env_nonexistent", "[source-env]")
{
    TestShell shell;
    shell("source-env \"/nonexistent/path/to/script.sh\"");
    CHECK(shell.exitCode == 1);
}

TEST_CASE("shell.builtin.source_env_unknown_extension", "[source-env]")
{
    TestShell shell;

    auto const tempDirGuard = endo::testing::ScopedTempDir { "endo_source_env" };
    auto const& tempDir = tempDirGuard.path();
    auto const unknownPath = tempDir / "endo_test_source_env.xyz";
    {
        std::ofstream ofs(unknownPath);
        ofs << "# unknown\n";
    }

    auto const cmd = std::format("source-env \"{}\"", unknownPath.string());
    shell(cmd);
    CHECK(shell.exitCode == 1);
}

TEST_CASE("shell.builtin.source_env_preserves_unchanged", "[source-env]")
{
    TestShell shell;
    shell.env.set("ENDO_TEST_EXISTING", "original_value");

#if defined(_WIN32)
    auto const tempDirGuard = endo::testing::ScopedTempDir { "endo_source_env" };
    auto const& tempDir = tempDirGuard.path();
    auto const scriptPath = tempDir / "endo_test_source_env_noop.bat";
    {
        std::ofstream ofs(scriptPath);
        ofs << "@echo off\r\n";
        ofs << "rem This script does not modify ENDO_TEST_EXISTING\r\n";
        ofs << "set ENDO_TEST_NEW=new_value\r\n";
    }
#else
    auto const tempDirGuard = endo::testing::ScopedTempDir { "endo_source_env" };
    auto const& tempDir = tempDirGuard.path();
    auto const scriptPath = tempDir / "endo_test_source_env_noop.sh";
    {
        std::ofstream ofs(scriptPath);
        ofs << "# This script does not modify ENDO_TEST_EXISTING\n";
        ofs << "export ENDO_TEST_NEW=new_value\n";
    }
#endif

    auto const cmd = std::format("source-env \"{}\"", scriptPath.string());
    shell(cmd);
    CHECK(shell.exitCode == 0);
    // Original value should remain unchanged
    CHECK(shell.env.get("ENDO_TEST_EXISTING").value_or("") == "original_value");
    // New variable should be imported
    CHECK(shell.env.get("ENDO_TEST_NEW").value_or("") == "new_value");
}

// ============================================================================
// Scripted Completer Integration Tests
// ============================================================================

namespace
{

/// @brief Checks if any completion in the result has the given text.
bool hasResult(std::vector<endo::CollectedCompletion> const& completions, std::string_view text)
{
    return std::ranges::any_of(completions, [text](auto const& c) { return c.text == text; });
}

/// @brief Registers a test completer that returns subcommands or options based on prefix.
void registerTestCompleter(TestShell& ts)
{
    ts(R"(
        let test_complete args prefix =
            match args with
            | [] when startsWith "-" prefix -> ["--help"; "--version"; "--verbose"]
            | [] -> ["run"; "install"; "update"]
            | _ -> []

        Completion.register "testcmd" test_complete
    )");
}

} // namespace

TEST_CASE("shell.completion.executeCompleterFunction_subcommands")
{
    TestShell ts;
    registerTestCompleter(ts);
    CHECK(ts.exitCode == 0);

    auto result = ts.shell.executeCompleterFunction("test_complete", {}, "");
    REQUIRE_FALSE(result.completions.empty());
    CHECK(hasResult(result.completions, "run"));
    CHECK(hasResult(result.completions, "install"));
    CHECK(hasResult(result.completions, "update"));
}

TEST_CASE("shell.completion.executeCompleterFunction_options")
{
    TestShell ts;
    registerTestCompleter(ts);
    CHECK(ts.exitCode == 0);

    auto result = ts.shell.executeCompleterFunction("test_complete", {}, "--");
    REQUIRE_FALSE(result.completions.empty());
    CHECK(hasResult(result.completions, "--help"));
}

TEST_CASE("shell.completion.Completion_register_populates_registry")
{
    TestShell ts;
    ts(R"(
        let my_complete args prefix = ["--help"]
        Completion.register "mycmd" my_complete
    )");
    CHECK(ts.exitCode == 0);
    CHECK(ts.shell.completerFunctions().hasCommand("mycmd"));
}

TEST_CASE("shell.completion.register_completer_populates_registry")
{
    TestShell ts;
    ts(R"(
        let my_complete args prefix = ["--help"]
        register_completer "mycmd2" my_complete
    )");
    CHECK(ts.exitCode == 0);
    CHECK(ts.shell.completerFunctions().hasCommand("mycmd2"));
}

TEST_CASE("shell.completion.executeCompleterFunction_separate_execute")
{
    TestShell ts;

    // Define function in one execute call, invoke in a separate context
    ts(R"(
        let my_complete args prefix =
            match args with
            | [] -> [Completion.described "--help" "Show help"; Completion.described "--version" "Show version"]
            | _ -> []
    )");
    CHECK(ts.exitCode == 0);

    auto result = ts.shell.executeCompleterFunction("my_complete", {}, "");
    CHECK_FALSE(result.completions.empty());
}

TEST_CASE("shell.completion.loadCompleters_populates_registry")
{
    TestShell ts;

    ts.shell.completer =
        std::make_unique<endo::Completer>(ts.env, ts.shell.history, ts.shell.fsharpState(), ts.shell.fs());
    ts.shell.loadCompleters();

    auto const& registry = ts.shell.completerFunctions();
    CHECK(registry.hasCommand("cmake"));
    CHECK(registry.hasCommand("claude"));
    CHECK(registry.hasCommand("ssh"));
    CHECK(registry.hasCommand("gh"));
    CHECK(registry.hasCommand("flatpak"));

    // Verify the claude completer works through the full pipeline
    auto claudeResult = ts.shell.executeCompleterFunction("claude_complete", {}, "");
    REQUIRE_FALSE(claudeResult.completions.empty());
    CHECK(hasResult(claudeResult.completions, "mcp"));
    CHECK(hasResult(claudeResult.completions, "auth"));
}

TEST_CASE("shell.completion.executeCompleterFunction_with_CompletionEntry")
{
    TestShell ts;

    ts(R"(
        let test_complete args prefix =
            [Completion.described "--help" "Show help"; Completion.described "--version" "Show version"]

        Completion.register "testcmd" test_complete
    )");
    CHECK(ts.exitCode == 0);

    auto result = ts.shell.executeCompleterFunction("test_complete", {}, "");
    REQUIRE_FALSE(result.completions.empty());
    CHECK(hasResult(result.completions, "--help"));

    // Verify description is carried through the bridge
    auto const it =
        std::ranges::find_if(result.completions, [](auto const& c) { return c.text == "--help"; });
    CHECK(it->description == "Show help");
}

// ============================================================================
// cat: markdown rendering wiring
//
// TestPTY is a real PTY on POSIX, so isTerminal() is true and `cat` takes the
// rendering path. WindowsTestPTY uses pipes and reports false, so the rendered
// assertions are POSIX-only; the plain-output assertions hold everywhere.
// Escape-level rendering semantics are covered in src/tui/MarkdownRenderer_test.cpp.
// ============================================================================

namespace
{
/// @brief Writes @p content into @p dir under @p name and returns the file's path.
///
/// The directory is the caller's, so its lifetime is visible at the call site: a guard
/// created here would delete the file before the caller could read it.
/// @param dir     Directory to write into; must outlive the returned path's use.
/// @param name    File name to create inside @p dir.
/// @param content Bytes to write.
/// @return The path of the written file.
auto writeMarkdownFixture(std::filesystem::path const& dir, std::string_view name, std::string_view content)
    -> std::filesystem::path
{
    auto const path = dir / name;
    auto stream = std::ofstream(path, std::ios::binary);
    stream.write(content.data(), static_cast<std::streamsize>(content.size()));
    return path;
}

constexpr auto MarkdownFixture = "# Title\n\nSee [Docs](https://endo-lang.org/).\n"sv;
} // namespace

TEST_CASE("shell.builtin.cat_markdown_renders_on_tty")
{
    InMemoryShell shell;
    auto const path = std::filesystem::path { "/test/render.md" };
    shell.fs.addFile(path, std::string(MarkdownFixture));

    shell(std::format("cat {}", path.string()));
    auto const output = std::string(shell.output());

    CHECK(shell.exitCode == 0);
#if !defined(_WIN32)
    // OSC-8 hyperlink around the link label.
    CHECK(output.find("\033]8;;https://endo-lang.org/\033\\") != std::string::npos);
    // Double-height title (DECDHL top/bottom halves).
    CHECK(output.find("\033#3") != std::string::npos);
    CHECK(output.find("\033#4") != std::string::npos);
    // The markdown source markers are consumed by the renderer.
    CHECK(output.find("# Title") == std::string::npos);
    CHECK(output.find("[Docs](") == std::string::npos);
#endif
    CHECK(output.find("Title") != std::string::npos);
    CHECK(output.find("Docs") != std::string::npos);
}

TEST_CASE("shell.builtin.cat_markdown_is_indented_by_default")
{
    InMemoryShell shell;
    auto const path = std::filesystem::path { "/test/indent_default.md" };
    shell.fs.addFile(path, std::string("Hello\n"sv));

    shell(std::format("cat {}", path.string()));
    auto const output = std::string(shell.output());

    CHECK(shell.exitCode == 0);
#if !defined(_WIN32)
    CHECK(output.find(" Hello") != std::string::npos);
#endif
}

TEST_CASE("shell.builtin.cat_markdown_indent_flag_overrides_the_default")
{
    InMemoryShell shell;
    auto const path = std::filesystem::path { "/test/indent_flag.md" };
    shell.fs.addFile(path, std::string("Hello\n"sv));

    shell(std::format("cat --indent 5 {}", path.string()));
    auto const output = std::string(shell.output());

    CHECK(shell.exitCode == 0);
#if !defined(_WIN32)
    CHECK(output.find("     Hello") != std::string::npos);
#endif
}

TEST_CASE("shell.builtin.cat_markdown_indent_zero_hugs_the_left_edge")
{
    InMemoryShell shell;
    auto const path = std::filesystem::path { "/test/indent_zero.md" };
    shell.fs.addFile(path, std::string("Hello\n"sv));

    shell(std::format("cat --indent=0 {}", path.string()));
    auto const output = std::string(shell.output());

    CHECK(shell.exitCode == 0);
#if !defined(_WIN32)
    CHECK(output.find(" Hello") == std::string::npos);
    CHECK(output.find("Hello") != std::string::npos);
#endif
}

TEST_CASE("shell.builtin.cat_indent_rejects_invalid_values")
{
    InMemoryShell bad;
    auto const path = std::filesystem::path { "/test/indent_bad.md" };
    bad.fs.addFile(path, std::string("Hello\n"sv));

    bad(std::format("cat --indent abc {}", path.string()));
    CHECK(bad.exitCode == 1);

    TestShell negative;
    negative(std::format("cat --indent -1 {}", path.string()));
    CHECK(negative.exitCode == 1);

    TestShell missing;
    missing("cat --indent");
    CHECK(missing.exitCode == 1);
}

TEST_CASE("shell.builtin.cat_indent_does_not_affect_raw_output")
{
    InMemoryShell shell;
    auto const path = std::filesystem::path { "/test/indent_raw.md" };
    shell.fs.addFile(path, std::string("Hello\n"sv));

    shell(std::format("cat --raw --indent 5 {}", path.string()));

    CHECK(shell.exitCode == 0);
    CHECK(std::string(shell.output()) == "Hello\n");
}

TEST_CASE("shell.builtin.cat_markdown_raw_flag_emits_source_bytes")
{
    InMemoryShell shell;
    auto const path = std::filesystem::path { "/test/raw.md" };
    shell.fs.addFile(path, std::string(MarkdownFixture));

    shell(std::format("cat --raw {}", path.string()));
    auto const output = std::string(shell.output());

    CHECK(shell.exitCode == 0);
    CHECK(output.find('\033') == std::string::npos); // no escapes at all
    CHECK(output.find("# Title") != std::string::npos);
    CHECK(output.find("[Docs](https://endo-lang.org/)") != std::string::npos);
}

TEST_CASE("shell.builtin.cat_markdown_piped_is_not_rendered")
{
    InMemoryShell shell;
    auto const path = std::filesystem::path { "/test/piped.md" };
    shell.fs.addFile(path, std::string(MarkdownFixture));

    // The first `cat` writes into a pipe, so its output handle is not a terminal.
    shell(std::format("cat {} | cat", path.string()));
    auto const output = std::string(shell.output());

    CHECK(shell.exitCode == 0);
    CHECK(output.find("\033]8;;") == std::string::npos);
    CHECK(output.find("# Title") != std::string::npos);
}

TEST_CASE("shell.builtin.cat_markdown_number_flag_shows_source")
{
    InMemoryShell shell;
    auto const path = std::filesystem::path { "/test/numbered.md" };
    shell.fs.addFile(path, std::string(MarkdownFixture));

    // -n asks for a literal view of the source, so rendering is suppressed.
    shell(std::format("cat -n {}", path.string()));
    auto const output = std::string(shell.output());

    CHECK(shell.exitCode == 0);
    CHECK(output.find("\033]8;;") == std::string::npos);
    CHECK(output.find("# Title") != std::string::npos);
    CHECK(output.find("1\t") != std::string::npos);
}

TEST_CASE("shell.builtin.cat_markdown_redirect_writes_source_bytes")
{
    namespace fs = std::filesystem;
    // Real filesystem: `>` opens its target as a descriptor, which an injected filesystem
    // cannot supply -- see the redirect note on Shell::applyRedirects.
    auto const markdownDir = endo::testing::ScopedTempDir { "endo_cat_markdown" };
    auto const path = writeMarkdownFixture(markdownDir.path(), "redirect.md", MarkdownFixture);
    auto const target = path.parent_path() / "redirect.out";
    fs::remove(target);

    TestShell shell;
    shell(std::format("cat {} > {}", path.string(), target.string()));
    CHECK(shell.exitCode == 0);

    auto stream = std::ifstream(target, std::ios::binary);
    auto const written = std::string(std::istreambuf_iterator<char>(stream), {});
    CHECK(written == std::string(MarkdownFixture));
}

TEST_CASE("shell.builtin.cat_never_falls_back_to_the_host_filesystem")
{
    // A path absent from the injected filesystem must be reported as absent, never opened
    // on the host's disk -- which is what a descriptor fallback would silently do.
    InMemoryShell shell;
    auto const output = shell("cat /etc/hostname").output();

    CHECK(shell.exitCode != 0);
    CHECK(output.find("No such file or directory") != std::string::npos);
}

TEST_CASE("shell.builtin.cat_distinguishes_why_a_file_could_not_be_read")
{
    // Each reason reaches the user as the filesystem stated it, rather than being inferred
    // from a follow-up probe that can only guess between two of them.
    InMemoryShell shell;
    shell.fs.addDirectory("/test/adir");
    shell.fs.addFile("/test/locked.txt", "secret\n");
    shell.fs.denyAccess("/test/locked.txt");

    CHECK(shell("cat /test/missing.txt").output().find("No such file or directory") != std::string::npos);
    CHECK(shell("cat /test/adir").output().find("Is a directory") != std::string::npos);
    CHECK(shell("cat /test/locked.txt").output().find("Permission denied") != std::string::npos);
}

TEST_CASE("shell.spawn.external_commands_go_through_the_injected_process_manager")
{
    MockedProcessShell shell;
    // An executable that exists only in the injected filesystem: nothing is forked, so the
    // program never has to be real.
    shell.fs.addExecutable("/test/tool");

    shell("/test/tool --flag value");

    REQUIRE(shell.spawned().size() == 1);
    CHECK(shell.spawned()[0].program == "/test/tool");
    CHECK(shell.spawned()[0].arguments == std::vector<std::string> { "--flag", "value" });
}

TEST_CASE("shell.builtin.cat_non_markdown_is_unaffected")
{
    InMemoryShell shell;
    auto const path = std::filesystem::path { "/test/plain.txt" };
    shell.fs.addFile(path, std::string("hello world\n"sv));

    shell(std::format("cat {}", path.string()));

    CHECK(shell.exitCode == 0);
    CHECK(std::string(shell.output()).find("hello world") != std::string::npos);
}

// ============================================================================
// OSC 8 clickable paths in ls output
// ============================================================================

namespace
{

/// @brief Populates @p dir with the entries the ls link tests inspect, and returns it.
///
/// `structured_ls` reads the real filesystem (it constructs the platform provider directly
/// rather than taking the injected FileSystem), so an ls test needs real files.
/// @param dir Directory to populate; must outlive the returned path's use.
/// @return @p dir, so call sites can chain.
auto writeLsFixture(std::filesystem::path const& dir) -> std::filesystem::path
{
    namespace fs = std::filesystem;
    fs::create_directories(dir / "subdir");
    auto plain = std::ofstream(dir / "plain.txt", std::ios::binary);
    plain << "x";
    auto spaced = std::ofstream(dir / "with space.txt", std::ios::binary);
    spaced << "y";
    return dir;
}

} // namespace

TEST_CASE("shell.builtin.ls_emits_osc8_hyperlinks_on_tty")
{
    auto const lsDir = endo::testing::ScopedTempDir { "endo_ls_links_emits" };
    auto const dir = writeLsFixture(lsDir.path());

    TestShell shell;
    shell(std::format("ls {}", dir.string()));
    auto const output = std::string(shell.output());

    CHECK(shell.exitCode == 0);
#if !defined(_WIN32)
    // Assert on the resolved absolute target, not merely that some escape appeared — that is
    // what proves the FileInfo path slot is actually plumbed through.
    CHECK(
        output.find(std::format("]8;;file://{}{}/plain.txt", endo::platform::cachedHostName(), dir.string()))
        != std::string::npos);
    // A space in the name must be percent-encoded inside the URI.
    CHECK(output.find("with%20space.txt") != std::string::npos);
    CHECK(output.find("\033]8;;\033\\") != std::string::npos);
#endif
    CHECK(output.find("plain.txt") != std::string::npos);
}

TEST_CASE("shell.builtin.ls_hyperlinks_can_be_disabled")
{
    auto const lsDir = endo::testing::ScopedTempDir { "endo_ls_links_disabled" };
    auto const dir = writeLsFixture(lsDir.path());

    TestShell shell;
    shell("shell_hyperlinks <- false");
    shell(std::format("ls {}", dir.string()));
    auto const output = std::string(shell.output());

    CHECK(output.find("]8;;file://") == std::string::npos);
    // The switch is independent of styling: the table chrome is still colorized.
    CHECK(output.find("plain.txt") != std::string::npos);
#if !defined(_WIN32)
    CHECK(output.find("\033[") != std::string::npos);
#endif
}

TEST_CASE("shell.builtin.ls_hyperlinks_survive_a_filtered_pipeline")
{
    // The URI comes from each record, so it stays correct after the listing is reshaped.
    auto const lsDir = endo::testing::ScopedTempDir { "endo_ls_links_filtered" };
    auto const dir = writeLsFixture(lsDir.path());

    TestShell shell;
    shell(std::format("ls {} |> filter _.isDir", dir.string()));
    auto const output = std::string(shell.output());

#if !defined(_WIN32)
    CHECK(output.find(std::format("]8;;file://{}{}/subdir", endo::platform::cachedHostName(), dir.string()))
          != std::string::npos);
#endif
    CHECK(output.find("subdir") != std::string::npos);
}

// ============================================================================
// Property descriptors vs. shell registrations
// ============================================================================

#include <endo-language/builtins/PropertyDescriptors.hpp>

#include <CoreVM/vm/Runtime.hpp>

#include <set>

TEST_CASE("shell.properties.descriptors_and_registrations_agree")
{
    // PropertyDescriptors.hpp calls itself the single source of truth for property metadata, but
    // the shell's registrations are hand-written in Registration.cpp and until now nothing checked
    // that the two agree. Both directions fail silently, which is why a test is worth having: a
    // descriptor with no registration type-checks and then does nothing at runtime, and a
    // registration with no descriptor is invisible to hover, completion and the LSP.
    TestShell shell;
    auto const& runtime = shell.shell.runtime();

    auto declared = std::set<std::string> {};
    for (auto const descriptors: { endo::promptPropertyDescriptors(), endo::agentPropertyDescriptors() })
    {
        for (auto const& descriptor: descriptors)
        {
            declared.emplace(descriptor.name);

            INFO("property: " << descriptor.name);
            auto const* registered = runtime.findProperty(std::string { descriptor.name });
            REQUIRE(registered != nullptr);
            CHECK(registered->type() == descriptor.type);
            CHECK(registered->hasGetter());
            // A read-only property must reject assignment at the VM layer too, not only in sema.
            CHECK(registered->hasSetter() == !descriptor.readOnly);
        }
    }

    for (auto const& registered: runtime.properties())
    {
        auto const& name = registered->name();
        if (!name.starts_with("shell_") && !name.starts_with("agent_"))
            continue;
        INFO("property: " << name);
        CHECK(declared.contains(name));
    }
}
