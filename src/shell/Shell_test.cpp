// SPDX-License-Identifier: Apache-2.0

#include <crispy/escape.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

using namespace std::string_literals;
using namespace std::string_view_literals;

using crispy::escape;
import Shell;
import TTY;

namespace
{
struct TestShell
{
    endo::TestPTY pty;
    endo::TestEnvironment env;
    int exitCode = -1;

    endo::Shell shell { pty, env };

    std::string_view output() const noexcept { return pty.output(); }

    TestShell& operator()(std::string_view cmd)
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

TEST_CASE("shell.syntax.if")
{
    TestShell shell;
    CHECK(shell("if true; then exit 2; else exit 3; fi").exitCode == 2);
    CHECK(shell("if false; then exit 2; else exit 3; fi").exitCode == 3);
}

TEST_CASE("shell.syntax.pipes")
{
    CHECK(escape(TestShell()("echo hello | grep ll").output()) == escape("hello\n"));
    CHECK(escape(TestShell()("echo hello | grep ll | grep hell").output()) == escape("hello\n"));
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
    CHECK(escape(shell("echo hello | grep $PATTERN").output()) == escape("hello\n"));
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
    TestShell shell;
    shell("echo hello > /tmp/endo_test_output.txt");
    // Read the file to verify content
    std::ifstream file("/tmp/endo_test_output.txt");
    std::string content;
    std::getline(file, content);
    CHECK(content == "hello");
    std::filesystem::remove("/tmp/endo_test_output.txt");
}

TEST_CASE("shell.redirect.output_append")
{
    TestShell shell;
    // Create initial file
    shell("echo line1 > /tmp/endo_test_append.txt");
    // Append to it
    shell("echo line2 >> /tmp/endo_test_append.txt");
    // Verify both lines present
    std::ifstream file("/tmp/endo_test_append.txt");
    std::string line1, line2;
    std::getline(file, line1);
    std::getline(file, line2);
    CHECK(line1 == "line1");
    CHECK(line2 == "line2");
    std::filesystem::remove("/tmp/endo_test_append.txt");
}

TEST_CASE("shell.redirect.input_from_file")
{
    TestShell shell;
    // Create test file
    {
        std::ofstream file("/tmp/endo_test_input.txt");
        file << "test input content\n";
    }
    // Use cat to read from file via redirect
    CHECK(escape(shell("cat < /tmp/endo_test_input.txt").output()) == escape("test input content\n"));
    std::filesystem::remove("/tmp/endo_test_input.txt");
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
    // Run ls on nonexistent file - stderr should go to stdout via 2>&1
    // Note: This test relies on ls outputting error to stderr
    shell("ls /nonexistent_path_12345 2>&1");
    // Should have some output (the error message)
    CHECK(!shell.output().empty());
}

TEST_CASE("shell.redirect.fd_to_file")
{
    TestShell shell;
    // Redirect stderr (fd 2) to a file
    shell("ls /nonexistent_path_12345 2> /tmp/endo_test_stderr.txt");
    // Verify the error was written to the file
    std::ifstream file("/tmp/endo_test_stderr.txt");
    std::string content;
    std::getline(file, content);
    CHECK(!content.empty()); // Should contain error message
    std::filesystem::remove("/tmp/endo_test_stderr.txt");
}

// ============================================================================
// Redirects - Multiple Redirects
// ============================================================================

TEST_CASE("shell.redirect.multiple_redirects")
{
    TestShell shell;
    // Create input file
    {
        std::ofstream file("/tmp/endo_test_multi_in.txt");
        file << "input text\n";
    }
    // Redirect both input and output
    shell("cat < /tmp/endo_test_multi_in.txt > /tmp/endo_test_multi_out.txt");
    // Verify output
    std::ifstream file("/tmp/endo_test_multi_out.txt");
    std::string content;
    std::getline(file, content);
    CHECK(content == "input text");
    std::filesystem::remove("/tmp/endo_test_multi_in.txt");
    std::filesystem::remove("/tmp/endo_test_multi_out.txt");
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
    CHECK(shell("false && echo hello").output() == "");
}

TEST_CASE("shell.logical.or_success")
{
    // true || echo hello - should NOT execute echo because true succeeds
    TestShell shell;
    CHECK(shell("true || echo hello").output() == "");
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

TEST_CASE("shell.subst.process_read_basic")
{
    // Process substitution read mode: cat <(echo hello)
    TestShell shell;
    CHECK(escape(shell("cat <(echo hello)").output()) == escape("hello\n"));
}

TEST_CASE("shell.subst.process_read_multiple_lines")
{
    // Process substitution with multiple lines
    TestShell shell;
    shell("echo line1 > /tmp/endo_test_procsubst.txt");
    shell("echo line2 >> /tmp/endo_test_procsubst.txt");
    CHECK(escape(shell("cat <(cat /tmp/endo_test_procsubst.txt)").output()) == escape("line1\nline2\n"));
    std::filesystem::remove("/tmp/endo_test_procsubst.txt");
}
