// SPDX-License-Identifier: Apache-2.0
#include <shell/Shell.h>

#include <catch2/catch.hpp>

namespace
{
struct TestShell
{
    std::stringstream in;
    std::stringstream out;
    std::stringstream err;
    crush::TestEnvironment env;
    int exitCode = -1;

    crush::Shell shell { in, out, err, env }; // TODO: pass custom reporter

    TestShell& operator()(std::string_view cmd)
    {
        exitCode = shell.execute(std::string(cmd));
        UNSCOPED_INFO("stderr: " + err.str());
        // TODO: print report if it is not empty
        return *this;
    }
};
} // namespace

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
    TestShell shell;
    CHECK(shell("echo -n hello | grep ll").out.str() == "hello");
}

// TEST_CASE("shell.syntax.&& ||")
// {
//     TestShell shell;
//     CHECK(shell("true && exit 2 || exit 3") == 2);
//     CHECK(shell("false && exit 2 || exit 3") == 3);
//     CHECK(shell("false || true") == 0);
// }
