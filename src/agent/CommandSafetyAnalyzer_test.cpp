// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <agent/CommandSafetyAnalyzer.hpp>

using namespace endo::agent;

TEST_CASE("CommandSafetyAnalyzer.readonly_commands", "[agent][safety]")
{
    CHECK(CommandSafetyAnalyzer::classify("ls -la").risk == ToolRisk::ReadOnly);
    CHECK(CommandSafetyAnalyzer::classify("cat /etc/hosts").risk == ToolRisk::ReadOnly);
    CHECK(CommandSafetyAnalyzer::classify("find . -name '*.cpp'").risk == ToolRisk::ReadOnly);
    CHECK(CommandSafetyAnalyzer::classify("wc -l file.txt").risk == ToolRisk::ReadOnly);
    CHECK(CommandSafetyAnalyzer::classify("echo hello").risk == ToolRisk::ReadOnly);
    CHECK(CommandSafetyAnalyzer::classify("pwd").risk == ToolRisk::ReadOnly);
    CHECK(CommandSafetyAnalyzer::classify("head -20 file.txt").risk == ToolRisk::ReadOnly);
    CHECK(CommandSafetyAnalyzer::classify("tail -f log.txt").risk == ToolRisk::ReadOnly);
    CHECK(CommandSafetyAnalyzer::classify("diff file1 file2").risk == ToolRisk::ReadOnly);
}

TEST_CASE("CommandSafetyAnalyzer.mutating_commands", "[agent][safety]")
{
    CHECK(CommandSafetyAnalyzer::classify("make -j8").risk == ToolRisk::Mutating);
    CHECK(CommandSafetyAnalyzer::classify("cmake --build .").risk == ToolRisk::Mutating);
    CHECK(CommandSafetyAnalyzer::classify("npm install").risk == ToolRisk::Mutating);
    CHECK(CommandSafetyAnalyzer::classify("cp file1 file2").risk == ToolRisk::Mutating);
    CHECK(CommandSafetyAnalyzer::classify("mv old new").risk == ToolRisk::Mutating);
    CHECK(CommandSafetyAnalyzer::classify("mkdir -p /tmp/test").risk == ToolRisk::Mutating);
    CHECK(CommandSafetyAnalyzer::classify("python script.py").risk == ToolRisk::Mutating);
    CHECK(CommandSafetyAnalyzer::classify("gcc -o hello hello.c").risk == ToolRisk::Mutating);
    CHECK(CommandSafetyAnalyzer::classify("cargo build").risk == ToolRisk::Mutating);
    CHECK(CommandSafetyAnalyzer::classify("pip install requests").risk == ToolRisk::Mutating);
}

TEST_CASE("CommandSafetyAnalyzer.destructive_patterns", "[agent][safety]")
{
    CHECK(CommandSafetyAnalyzer::classify("rm -rf /").risk == ToolRisk::Destructive);
    CHECK(CommandSafetyAnalyzer::classify("rm -rf /*").risk == ToolRisk::Destructive);
    CHECK(CommandSafetyAnalyzer::classify("rm --no-preserve-root /").risk == ToolRisk::Destructive);
    CHECK(CommandSafetyAnalyzer::classify("mkfs.ext4 /dev/sda1").risk == ToolRisk::Destructive);
    CHECK(CommandSafetyAnalyzer::classify("dd if=/dev/zero of=/dev/sda").risk == ToolRisk::Destructive);
    CHECK(CommandSafetyAnalyzer::classify("shutdown -h now").risk == ToolRisk::Destructive);
    CHECK(CommandSafetyAnalyzer::classify("reboot").risk == ToolRisk::Destructive);
    CHECK(CommandSafetyAnalyzer::classify("chmod -R 777 /").risk == ToolRisk::Destructive);
}

TEST_CASE("CommandSafetyAnalyzer.interactive_commands_blocked", "[agent][safety]")
{
    CHECK(CommandSafetyAnalyzer::classify("vim file.txt").risk == ToolRisk::Blocked);
    CHECK(CommandSafetyAnalyzer::classify("vi file.txt").risk == ToolRisk::Blocked);
    CHECK(CommandSafetyAnalyzer::classify("nvim file.txt").risk == ToolRisk::Blocked);
    CHECK(CommandSafetyAnalyzer::classify("nano file.txt").risk == ToolRisk::Blocked);
    CHECK(CommandSafetyAnalyzer::classify("top").risk == ToolRisk::Blocked);
    CHECK(CommandSafetyAnalyzer::classify("htop").risk == ToolRisk::Blocked);
    CHECK(CommandSafetyAnalyzer::classify("man ls").risk == ToolRisk::Blocked);

    // Interactive is marked in the result.
    auto const result = CommandSafetyAnalyzer::classify("vim file.txt");
    CHECK(result.isInteractive);
}

TEST_CASE("CommandSafetyAnalyzer.interactive_git_commands", "[agent][safety]")
{
    CHECK(CommandSafetyAnalyzer::classify("git rebase -i HEAD~3").risk == ToolRisk::Blocked);
    CHECK(CommandSafetyAnalyzer::classify("git add -i").risk == ToolRisk::Blocked);
    CHECK(CommandSafetyAnalyzer::classify("git add -p").risk == ToolRisk::Blocked);
    CHECK(CommandSafetyAnalyzer::classify("git rebase --interactive HEAD~3").risk == ToolRisk::Blocked);
}

TEST_CASE("CommandSafetyAnalyzer.python_non_interactive", "[agent][safety]")
{
    // Python with -c flag or .py file is non-interactive.
    CHECK(CommandSafetyAnalyzer::classify("python -c 'print(1)'").risk == ToolRisk::Mutating);
    CHECK(CommandSafetyAnalyzer::classify("python3 script.py").risk == ToolRisk::Mutating);
    // Plain python without args is interactive.
    CHECK(CommandSafetyAnalyzer::classify("python").risk == ToolRisk::Blocked);
}

TEST_CASE("CommandSafetyAnalyzer.piped_commands_worst_risk", "[agent][safety]")
{
    // ls | grep is read-only (both segments are read-only).
    CHECK(CommandSafetyAnalyzer::classify("ls | grep foo").risk == ToolRisk::ReadOnly);

    // cat | rm -rf / — the rm segment is destructive.
    CHECK(CommandSafetyAnalyzer::classify("cat file.txt | rm -rf /").risk == ToolRisk::Destructive);

    // ls && make — make is mutating.
    CHECK(CommandSafetyAnalyzer::classify("ls && make").risk == ToolRisk::Mutating);
}

TEST_CASE("CommandSafetyAnalyzer.command_chaining", "[agent][safety]")
{
    CHECK(CommandSafetyAnalyzer::classify("ls && echo hello").risk == ToolRisk::ReadOnly);
    CHECK(CommandSafetyAnalyzer::classify("ls; make").risk == ToolRisk::Mutating);
    CHECK(CommandSafetyAnalyzer::classify("ls || vim file").risk == ToolRisk::Blocked);
}

TEST_CASE("CommandSafetyAnalyzer.extra_blocked_patterns", "[agent][safety]")
{
    auto const blocked = std::vector<std::string> { "curl", "wget" };
    CHECK(CommandSafetyAnalyzer::classify("curl http://example.com", blocked).risk == ToolRisk::Blocked);
    CHECK(CommandSafetyAnalyzer::classify("wget http://example.com", blocked).risk == ToolRisk::Blocked);
    // Without extra patterns, curl/wget are unknown → mutating.
    CHECK(CommandSafetyAnalyzer::classify("curl http://example.com").risk == ToolRisk::Mutating);
}

TEST_CASE("CommandSafetyAnalyzer.empty_and_whitespace", "[agent][safety]")
{
    CHECK(CommandSafetyAnalyzer::classify("").risk == ToolRisk::ReadOnly);
    CHECK(CommandSafetyAnalyzer::classify("   ").risk == ToolRisk::ReadOnly);
}

TEST_CASE("CommandSafetyAnalyzer.path_prefix_stripped", "[agent][safety]")
{
    CHECK(CommandSafetyAnalyzer::classify("/usr/bin/ls -la").risk == ToolRisk::ReadOnly);
    CHECK(CommandSafetyAnalyzer::classify("/bin/cat /etc/hosts").risk == ToolRisk::ReadOnly);
}

TEST_CASE("CommandSafetyAnalyzer.sudo_prefix", "[agent][safety]")
{
    // sudo + read-only command is still just read-only classification.
    CHECK(CommandSafetyAnalyzer::classify("sudo ls").risk == ToolRisk::ReadOnly);
    CHECK(CommandSafetyAnalyzer::classify("sudo cat /etc/shadow").risk == ToolRisk::ReadOnly);
    CHECK(CommandSafetyAnalyzer::classify("sudo rm -rf /").risk == ToolRisk::Destructive);
}

TEST_CASE("CommandSafetyAnalyzer.isInteractive_helper", "[agent][safety]")
{
    CHECK(CommandSafetyAnalyzer::isInteractive("vim file.txt"));
    CHECK(CommandSafetyAnalyzer::isInteractive("top"));
    CHECK(CommandSafetyAnalyzer::isInteractive("git rebase -i HEAD~3"));
    CHECK_FALSE(CommandSafetyAnalyzer::isInteractive("ls"));
    CHECK_FALSE(CommandSafetyAnalyzer::isInteractive("make"));
    CHECK_FALSE(CommandSafetyAnalyzer::isInteractive("python3 script.py"));
}

TEST_CASE("CommandSafetyAnalyzer.git_destructive_via_shell", "[agent][safety]")
{
    CHECK(CommandSafetyAnalyzer::classify("git push --force").risk == ToolRisk::Destructive);
    CHECK(CommandSafetyAnalyzer::classify("git push -f").risk == ToolRisk::Destructive);
    CHECK(CommandSafetyAnalyzer::classify("git reset --hard").risk == ToolRisk::Destructive);
    CHECK(CommandSafetyAnalyzer::classify("git clean -f").risk == ToolRisk::Destructive);
}

TEST_CASE("CommandSafetyAnalyzer.unknown_commands_default_mutating", "[agent][safety]")
{
    CHECK(CommandSafetyAnalyzer::classify("some_unknown_tool arg1 arg2").risk == ToolRisk::Mutating);
}
