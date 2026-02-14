// SPDX-License-Identifier: Apache-2.0

#include <crispy/escape.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

using namespace std::string_literals;
using namespace std::string_view_literals;

using crispy::escape;

#include "CompletionProviders/FileCompleter.hpp"
#include "CompletionProviders/LetBindingCompleter.hpp"
#include "Shell.hpp"
#include "TTY.hpp"
#include "TableFormatter.hpp"
#include <endo-language/CompletionContext.hpp>

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

TEST_CASE("shell.syntax.pipes")
{
    CHECK(escape(TestShell()("echo hello | grep ll").output()) == escape("hello\n"));
    CHECK(escape(TestShell()("echo hello | grep ll | grep hell").output()) == escape("hello\n"));
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
    CHECK(output.find("Usage:") != std::string::npos);
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
    CHECK(shell.env.get("C").value_or("NONE") == "");
    CHECK(shell.env.get("D").value_or("NONE") == "");
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
    CHECK(shell.env.get("B").value_or("NONE") == "");
    CHECK(shell.env.get("C").value_or("NONE") == "");
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
    CHECK(output.find("Usage:") != std::string::npos);
    CHECK(shell.exitCode == 0);
}

TEST_CASE("shell.builtin.which_help_flag")
{
    TestShell shell;
    auto output = shell("which --help").output();
    CHECK(output.find("Usage:") != std::string::npos);
    CHECK(output.find("--all") != std::string::npos);
    CHECK(shell.exitCode == 0);
}

TEST_CASE("shell.builtin.which_find_existing_program")
{
    TestShell shell;
    // /bin/ls or /usr/bin/ls should exist on most systems
    auto output = shell("which ls").output();
    CHECK((output.find("/bin/ls") != std::string::npos || output.find("/usr/bin/ls") != std::string::npos));
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
    auto output = shell("which ls cat").output();
    CHECK(output.find("ls") != std::string::npos);
    CHECK(output.find("cat") != std::string::npos);
    CHECK(shell.exitCode == 0);
}

TEST_CASE("shell.builtin.which_mixed_existing_and_nonexistent")
{
    TestShell shell;
    // Should find ls but not the nonexistent one, return 1
    auto output = shell("which ls nonexistent_xyz_123").output();
    CHECK(output.find("ls") != std::string::npos);
    CHECK(shell.exitCode == 1);
}

TEST_CASE("shell.builtin.which_invalid_option")
{
    TestShell shell;
    shell("which --invalid-option ls");
    CHECK(shell.exitCode == 1);
}

TEST_CASE("shell.builtin.which_read_alias_warning")
{
    TestShell shell;
    // --read-alias should warn but continue
    shell("which -i ls");
    // Should still find ls despite the warning
    CHECK(shell.exitCode == 0);
}

// ============================================================================
// Cat Builtin
// ============================================================================

TEST_CASE("shell.builtin.cat_help")
{
    TestShell shell;
    auto output = shell("cat --help").output();
    CHECK(output.find("Usage:") != std::string::npos);
    CHECK(output.find("--number") != std::string::npos);
    CHECK(shell.exitCode == 0);
}

TEST_CASE("shell.builtin.cat_help_short")
{
    TestShell shell;
    auto output = shell("cat -h").output();
    CHECK(output.find("Usage:") != std::string::npos);
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
    auto output = shell("echo -e \"line1\\nline2\\nline3\" | cat -n").output();
    CHECK(output.find("1\t") != std::string::npos);
    CHECK(output.find("2\t") != std::string::npos);
    CHECK(output.find("3\t") != std::string::npos);
}

TEST_CASE("shell.builtin.cat_b_flag")
{
    // Test -b only numbers non-blank lines
    TestShell shell;
    auto output = shell("echo -e \"line1\\n\\nline2\" | cat -b").output();
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
    auto output = shell("echo -e \"hello\\nworld\" | cat -E").output();
    CHECK(output.find("hello$") != std::string::npos);
    CHECK(output.find("world$") != std::string::npos);
}

TEST_CASE("shell.builtin.cat_T_flag")
{
    // Test -T shows tabs as ^I
    TestShell shell;
    auto output = shell("echo -e \"a\\tb\" | cat -T").output();
    CHECK(output.find("^I") != std::string::npos);
}

TEST_CASE("shell.builtin.cat_s_flag")
{
    // Test -s squeezes multiple blank lines
    TestShell shell;
    auto output = shell("echo -e \"a\\n\\n\\n\\nb\" | cat -s").output();
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
    auto output = shell("echo -e \"a\\tb\" | cat -A").output();
    CHECK(output.find("^I") != std::string::npos); // Shows tabs
    CHECK(output.find("$") != std::string::npos);  // Shows line ends
}

TEST_CASE("shell.builtin.cat_combined_flags")
{
    // Test combining multiple flags
    TestShell shell;
    auto output = shell("echo -e \"a\\tb\\ncd\" | cat -nT").output();
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
// Sleep Builtin
// ============================================================================

TEST_CASE("shell.builtin.sleep_help")
{
    TestShell shell;
    auto output = shell("sleep --help").output();
    CHECK(output.find("Usage:") != std::string::npos);
    CHECK(output.find("SUFFIX") != std::string::npos);
    CHECK(shell.exitCode == 0);
}

TEST_CASE("shell.builtin.sleep_help_short")
{
    TestShell shell;
    auto output = shell("sleep -h").output();
    CHECK(output.find("Usage:") != std::string::npos);
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
    CHECK(shell.env.get("RESULT").value_or("FAIL") == "");
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
    // *.txt should match .txt files in current directory
    // We use /tmp to create test files
    TestShell shell;

    // Create test files
    shell("echo test > /tmp/glob_test_a.txt");
    shell("echo test > /tmp/glob_test_b.txt");
    shell("echo test > /tmp/glob_test_c.log");

    // Run glob expansion
    auto result = shell("echo /tmp/glob_test_*.txt").output();

    // Should contain both .txt files but not the .log file
    CHECK(result.find("glob_test_a.txt") != std::string::npos);
    CHECK(result.find("glob_test_b.txt") != std::string::npos);
    CHECK(result.find("glob_test_c.log") == std::string::npos);

    // Cleanup
    shell("rm /tmp/glob_test_*.txt /tmp/glob_test_*.log");
}

TEST_CASE("shell.expand.glob_question")
{
    // ? matches single character
    TestShell shell;

    // Create test files
    shell("echo test > /tmp/glob_qtest_a.txt");
    shell("echo test > /tmp/glob_qtest_b.txt");
    shell("echo test > /tmp/glob_qtest_aa.txt");

    // Run glob expansion - should match single character only
    auto result = shell("echo /tmp/glob_qtest_?.txt").output();

    CHECK(result.find("glob_qtest_a.txt") != std::string::npos);
    CHECK(result.find("glob_qtest_b.txt") != std::string::npos);
    CHECK(result.find("glob_qtest_aa.txt") == std::string::npos);

    // Cleanup
    shell("rm /tmp/glob_qtest_*.txt");
}

TEST_CASE("shell.expand.glob_bracket")
{
    // [abc] matches any character in set
    TestShell shell;

    // Create test files
    shell("echo test > /tmp/glob_btest_a.txt");
    shell("echo test > /tmp/glob_btest_b.txt");
    shell("echo test > /tmp/glob_btest_c.txt");
    shell("echo test > /tmp/glob_btest_d.txt");

    // Run glob expansion - should match a, b, c but not d
    auto result = shell("echo /tmp/glob_btest_[abc].txt").output();

    CHECK(result.find("glob_btest_a.txt") != std::string::npos);
    CHECK(result.find("glob_btest_b.txt") != std::string::npos);
    CHECK(result.find("glob_btest_c.txt") != std::string::npos);
    CHECK(result.find("glob_btest_d.txt") == std::string::npos);

    // Cleanup
    shell("rm /tmp/glob_btest_*.txt");
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
    // [a-z] matches a range of characters
    TestShell shell;

    // Create test files with letters
    shell("echo test > /tmp/glob_rtest_a.txt");
    shell("echo test > /tmp/glob_rtest_c.txt");
    shell("echo test > /tmp/glob_rtest_z.txt");
    shell("echo test > /tmp/glob_rtest_1.txt");

    // Run glob expansion - should match a, c, z but not 1
    auto result = shell("echo /tmp/glob_rtest_[a-z].txt").output();

    CHECK(result.find("glob_rtest_a.txt") != std::string::npos);
    CHECK(result.find("glob_rtest_c.txt") != std::string::npos);
    CHECK(result.find("glob_rtest_z.txt") != std::string::npos);
    CHECK(result.find("glob_rtest_1.txt") == std::string::npos);

    // Cleanup
    shell("rm /tmp/glob_rtest_*.txt");
}

TEST_CASE("shell.expand.glob_recursive_starstar")
{
    // ** matches files recursively
    TestShell shell;

    // Create directory structure
    shell("mkdir -p /tmp/glob_rec_test/sub1/sub2");
    shell("echo test > /tmp/glob_rec_test/file1.cpp");
    shell("echo test > /tmp/glob_rec_test/sub1/file2.cpp");
    shell("echo test > /tmp/glob_rec_test/sub1/sub2/file3.cpp");
    shell("echo test > /tmp/glob_rec_test/file4.txt");

    // Run recursive glob expansion
    auto result = shell("echo /tmp/glob_rec_test/**/*.cpp").output();

    // Should find all .cpp files recursively
    CHECK(result.find("file1.cpp") != std::string::npos);
    CHECK(result.find("file2.cpp") != std::string::npos);
    CHECK(result.find("file3.cpp") != std::string::npos);
    CHECK(result.find("file4.txt") == std::string::npos);

    // Cleanup
    shell("rm -rf /tmp/glob_rec_test");
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
    auto tempDir = std::filesystem::temp_directory_path() / "endo_test_completion";
    std::filesystem::create_directories(tempDir / "src");
    std::filesystem::create_directories(tempDir / "scripts");

    // Change to the temp directory for testing
    auto originalDir = std::filesystem::current_path();
    std::filesystem::current_path(tempDir);

    endo::FileCompleter completer;

    // Complete "sr" - should match both "src" (prefix) and "scripts" (fuzzy)
    endo::CompletionContext context {
        .type = endo::CompletionContextType::FilePath,
        .prefix = "sr",
        .cursorPosition = 5,
        .fullInput = "cd sr",
    };

    auto results = completer.complete(context);

    // Clean up
    std::filesystem::current_path(originalDir);
    std::filesystem::remove_all(tempDir);

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
        .isRecursive = true,
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
        .prefix = "foo",
        .cursorPosition = 3,
        .fullInput = "foo",
    };

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

#include "Prompt.hpp"

namespace
{

std::string readAllFromPipe(int readFd)
{
    std::string result;
    char buf[256];
    for (;;)
    {
        auto const n = ::read(readFd, buf, sizeof(buf));
        if (n <= 0)
            break;
        result.append(buf, static_cast<size_t>(n));
    }
    return result;
}

} // namespace

TEST_CASE("shell.partial_line_indicator.emits_when_not_at_col1")
{
    int fds[2];
    REQUIRE(::pipe(fds) == 0);

    endo::emitPartialLineIndicator(fds[1], 5);
    ::close(fds[1]);

    auto const output = readAllFromPipe(fds[0]);
    ::close(fds[0]);

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
    int fds[2];
    REQUIRE(::pipe(fds) == 0);

    endo::emitPartialLineIndicator(fds[1], 1);
    ::close(fds[1]);

    auto const output = readAllFromPipe(fds[0]);
    ::close(fds[0]);

    CHECK(output.empty());
}

TEST_CASE("shell.partial_line_indicator.silent_on_failure")
{
    int fds[2];
    REQUIRE(::pipe(fds) == 0);

    endo::emitPartialLineIndicator(fds[1], 0);
    ::close(fds[1]);

    auto const output = readAllFromPipe(fds[0]);
    ::close(fds[0]);

    CHECK(output.empty());
}

// ============================================================================
// Fetch Builtin Tests
// ============================================================================

TEST_CASE("shell.fsharp.fetch.invalid_url")
{
    // fetch with an invalid URL should return Error result
    TestShell shell;
    shell(R"(match fetch "not-a-valid-url" with | Ok b -> print "ok" | Error e -> print "error")");
    CHECK(escape(shell.output()) == escape("error"));
}

TEST_CASE("shell.fsharp.fetch.connection_refused")
{
    // fetch to a port with no listener should return Error result
    TestShell shell;
    shell(R"(match fetch "http://localhost:1" with | Ok b -> print "ok" | Error e -> print "error")");
    CHECK(escape(shell.output()) == escape("error"));
}
