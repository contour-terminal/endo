// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "RequiredPaths.hpp"

using namespace endo;

TEST_CASE("canonicalizeForHistory.home_subpath", "[history][required-paths]")
{
    CHECK(canonicalizeForHistory("/home/u/projects/endo", "/home/u") == "~/projects/endo");
}

TEST_CASE("canonicalizeForHistory.exact_home", "[history][required-paths]")
{
    CHECK(canonicalizeForHistory("/home/u", "/home/u") == "~");
}

TEST_CASE("canonicalizeForHistory.outside_home", "[history][required-paths]")
{
    CHECK(canonicalizeForHistory("/tmp/x", "/home/u") == "/tmp/x");
    CHECK(canonicalizeForHistory("/etc/hosts", "/home/u") == "/etc/hosts");
}

TEST_CASE("canonicalizeForHistory.no_false_prefix_match", "[history][required-paths]")
{
    // /home/userx is NOT inside /home/user.
    CHECK(canonicalizeForHistory("/home/userx/a", "/home/user") == "/home/userx/a");
}

TEST_CASE("canonicalizeForHistory.home_with_trailing_slash", "[history][required-paths]")
{
    CHECK(canonicalizeForHistory("/home/u/projects", "/home/u/") == "~/projects");
    CHECK(canonicalizeForHistory("/home/u", "/home/u/") == "~");
}

TEST_CASE("canonicalizeForHistory.empty_home", "[history][required-paths]")
{
    CHECK(canonicalizeForHistory("/home/u/x", "") == "/home/u/x");
}

TEST_CASE("expandForLookup.tilde_slash", "[history][required-paths]")
{
    CHECK(expandForLookup("~/projects/endo", "/home/u") == "/home/u/projects/endo");
}

TEST_CASE("expandForLookup.bare_tilde", "[history][required-paths]")
{
    CHECK(expandForLookup("~", "/home/u") == "/home/u");
}

TEST_CASE("expandForLookup.absolute_unchanged", "[history][required-paths]")
{
    CHECK(expandForLookup("/tmp/x", "/home/u") == "/tmp/x");
}

TEST_CASE("canonicalize_expand_roundtrip", "[history][required-paths]")
{
    auto const home = std::string_view { "/home/u" };
    auto const original = std::string_view { "/home/u/projects/endo/file.txt" };
    auto const canonical = canonicalizeForHistory(original, home);
    CHECK(canonical == "~/projects/endo/file.txt");
    CHECK(expandForLookup(canonical, home) == original);
}

TEST_CASE("collectRequiredPaths.absolute_path", "[history][required-paths]")
{
    auto const argv = std::vector<std::string> { "vim", "/tmp/a.txt" };
    auto const paths = collectRequiredPaths(argv, "/home/u", "/home/u");
    REQUIRE(paths.size() == 1);
    CHECK(paths[0] == "/tmp/a.txt");
}

TEST_CASE("collectRequiredPaths.bare_name_is_not_a_path", "[history][required-paths]")
{
    // Bare filenames without `/` or path prefix are NOT treated as paths —
    // they could equally be command names resolved via $PATH.
    auto const argv = std::vector<std::string> { "vim", "a.txt" };
    auto const paths = collectRequiredPaths(argv, "/home/u", "/home/u");
    CHECK(paths.empty());
}

TEST_CASE("collectRequiredPaths.dotslash_relative_under_cwd", "[history][required-paths]")
{
    auto const argv = std::vector<std::string> { "vim", "./a.txt" };
    auto const paths = collectRequiredPaths(argv, "/home/u", "/home/u");
    REQUIRE(paths.size() == 1);
    CHECK(paths[0] == "~/a.txt");
}

TEST_CASE("collectRequiredPaths.subdir_relative_under_cwd", "[history][required-paths]")
{
    auto const argv = std::vector<std::string> { "vim", "notes/plan.md" };
    auto const paths = collectRequiredPaths(argv, "/home/u", "/home/u");
    REQUIRE(paths.size() == 1);
    CHECK(paths[0] == "~/notes/plan.md");
}

TEST_CASE("collectRequiredPaths.absolute_in_home_canonicalized", "[history][required-paths]")
{
    auto const argv = std::vector<std::string> { "vim", "/home/u/notes" };
    auto const paths = collectRequiredPaths(argv, "/home/u/projects", "/home/u");
    REQUIRE(paths.size() == 1);
    CHECK(paths[0] == "~/notes");
}

TEST_CASE("collectRequiredPaths.tilde_prefix", "[history][required-paths]")
{
    auto const argv = std::vector<std::string> { "cat", "~/notes/plan.md" };
    auto const paths = collectRequiredPaths(argv, "/tmp", "/home/u");
    REQUIRE(paths.size() == 1);
    CHECK(paths[0] == "~/notes/plan.md");
}

TEST_CASE("collectRequiredPaths.skips_bare_command_and_flags", "[history][required-paths]")
{
    auto const argv = std::vector<std::string> { "ls", "-la", "--color=auto" };
    auto const paths = collectRequiredPaths(argv, "/home/u", "/home/u");
    CHECK(paths.empty());
}

TEST_CASE("collectRequiredPaths.mixed_args", "[history][required-paths]")
{
    auto const argv = std::vector<std::string> { "cp", "-r", "/tmp/a", "/home/u/b" };
    auto const paths = collectRequiredPaths(argv, "/home/u", "/home/u");
    REQUIRE(paths.size() == 2);
    CHECK(paths[0] == "/tmp/a");
    CHECK(paths[1] == "~/b");
}

TEST_CASE("collectRequiredPaths.caps_at_max", "[history][required-paths]")
{
    auto argv = std::vector<std::string> { "rm" };
    for (auto i = 0; i < 20; ++i)
        argv.push_back("/tmp/f" + std::to_string(i));

    auto const paths = collectRequiredPaths(argv, "/home/u", "/home/u");
    CHECK(paths.size() == maxRequiredPaths);
}

TEST_CASE("collectRequiredPaths.skips_urls", "[history][required-paths]")
{
    // Classic fetch/clone URLs must not be recorded as required paths.
    auto const httpArgv = std::vector<std::string> { "curl", "https://example.com/a/b" };
    CHECK(collectRequiredPaths(httpArgv, "/home/u", "/home/u").empty());

    auto const gitArgv = std::vector<std::string> { "git", "clone", "git@github.com:org/repo" };
    CHECK(collectRequiredPaths(gitArgv, "/home/u", "/home/u").empty());

    auto const fileArgv = std::vector<std::string> { "open", "file:///tmp/a" };
    CHECK(collectRequiredPaths(fileArgv, "/home/u", "/home/u").empty());
}

TEST_CASE("collectRequiredPaths.deduplicates", "[history][required-paths]")
{
    auto const argv = std::vector<std::string> { "diff", "/tmp/a", "/tmp/a" };
    auto const paths = collectRequiredPaths(argv, "/home/u", "/home/u");
    REQUIRE(paths.size() == 1);
    CHECK(paths[0] == "/tmp/a");
}

TEST_CASE("collectRequiredPaths.empty_argv", "[history][required-paths]")
{
    auto const argv = std::vector<std::string> {};
    auto const paths = collectRequiredPaths(argv, "/home/u", "/home/u");
    CHECK(paths.empty());
}

TEST_CASE("collectRequiredPaths.only_command", "[history][required-paths]")
{
    auto const argv = std::vector<std::string> { "git" };
    auto const paths = collectRequiredPaths(argv, "/home/u", "/home/u");
    CHECK(paths.empty());
}
