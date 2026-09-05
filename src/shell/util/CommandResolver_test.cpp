// SPDX-License-Identifier: Apache-2.0

#include <shell/util/CommandResolver.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include <platform/testing/InMemoryFileSystem.hpp>
#include <platform/testing/TestEnvironmentProvider.hpp>

using namespace endo;

namespace
{

/// @brief A $PATH directory populated inside a filesystem private to one test.
///
/// CommandResolver already takes FileSystem const&, so injecting an in-memory one is the
/// designed seam: each test gets its own tree, with no shared path to collide over.
struct PathDir
{
    endo::InMemoryFileSystem fs;
    std::filesystem::path path { "/test/bin" };

    PathDir() { fs.addDirectory(path); }

    /// @brief Creates an executable file with the given name in the directory.
    void createExecutable(std::string const& name) { fs.addExecutable(path / name); }

    /// @brief Creates a file without the execute bit.
    void createNonExecutable(std::string const& name) { fs.addFile(path / name, ""); }
};

} // namespace

TEST_CASE("CommandResolver.findInPath.bare_name_found")
{
    PathDir dir;
#if defined(_WIN32)
    dir.createExecutable("testcmd.exe");
#else
    dir.createExecutable("testcmd");
#endif

    platform::TestEnvironmentProvider env;
    env.set("PATH", dir.path.string());
#if defined(_WIN32)
    env.set("PATHEXT", ".exe;.cmd;.bat");
#endif

    auto const resolver = CommandResolver(env, dir.fs);
    auto const result = resolver.findInPath("testcmd");
    REQUIRE(!result.empty());
    CHECK(std::filesystem::path(result).filename().string().starts_with("testcmd"));
}

TEST_CASE("CommandResolver.findInPath.not_found")
{
    PathDir dir;

    platform::TestEnvironmentProvider env;
    env.set("PATH", dir.path.string());
#if defined(_WIN32)
    env.set("PATHEXT", ".exe");
#endif

    auto const resolver = CommandResolver(env, dir.fs);
    auto const result = resolver.findInPath("nonexistent");
    CHECK(result.empty());
}

TEST_CASE("CommandResolver.findInPath.missing_PATH")
{
    platform::TestEnvironmentProvider env;
    // No PATH set at all.

    auto const fs = endo::InMemoryFileSystem {};
    auto const resolver = CommandResolver(env, fs);
    auto const result = resolver.findInPath("anything");
    CHECK(result.empty());
}

TEST_CASE("CommandResolver.findInPath.skips_nonexistent_directory")
{
    PathDir dir;
#if defined(_WIN32)
    dir.createExecutable("mycmd.exe");
    auto const pathValue = std::string("C:\\no_such_dir_12345") + ";" + dir.path.string();
    platform::TestEnvironmentProvider env;
    env.set("PATH", pathValue);
    env.set("PATHEXT", ".exe");
#else
    dir.createExecutable("mycmd");
    auto const pathValue = std::string("/no_such_dir_12345") + ":" + dir.path.string();
    platform::TestEnvironmentProvider env;
    env.set("PATH", pathValue);
#endif

    auto const resolver = CommandResolver(env, dir.fs);
    auto const result = resolver.findInPath("mycmd");
    REQUIRE(!result.empty());
    CHECK(std::filesystem::path(result).filename().string().starts_with("mycmd"));
}

#if defined(_WIN32)
TEST_CASE("CommandResolver.findInPath.PATHEXT_resolution")
{
    PathDir dir;
    // Create testapp.cmd — should be found when searching for "testapp"
    dir.createExecutable("testapp.cmd");

    platform::TestEnvironmentProvider env;
    env.set("PATH", dir.path.string());
    env.set("PATHEXT", ".exe;.cmd;.bat");

    auto const resolver = CommandResolver(env, dir.fs);
    auto const result = resolver.findInPath("testapp");
    REQUIRE(!result.empty());
    CHECK(result.ends_with(".cmd"));
}

TEST_CASE("CommandResolver.findInPath.prefers_PATHEXT_over_extensionless")
{
    // Regression: a bare command name must resolve to its PATHEXT executable, not to an
    // extensionless shim of the same name that happens to sit earlier in the directory.
    platform::testing::InMemoryFileSystem fs;
    fs.addDirectory("/bin");
    fs.addExecutable("/bin/docker");     // extensionless shim — not a runnable Windows command
    fs.addExecutable("/bin/docker.exe"); // the real CLI

    platform::TestEnvironmentProvider env;
    env.set("PATH", "/bin");
    env.set("PATHEXT", ".exe;.cmd;.bat");

    auto const resolver = CommandResolver(env, fs);
    auto const result = resolver.findInPath("docker");
    REQUIRE(!result.empty());
    CHECK(result.ends_with("docker.exe"));
}

TEST_CASE("CommandResolver.findInPath.extensionless_only_is_not_found")
{
    // On Windows an extensionless file is not a runnable command, so a bare lookup that
    // can only find one must report not-found rather than returning the unrunnable file.
    platform::testing::InMemoryFileSystem fs;
    fs.addDirectory("/bin");
    fs.addExecutable("/bin/docker");

    platform::TestEnvironmentProvider env;
    env.set("PATH", "/bin");
    env.set("PATHEXT", ".exe;.cmd;.bat");

    auto const resolver = CommandResolver(env, fs);
    CHECK(resolver.findInPath("docker").empty());
}

TEST_CASE("CommandResolver.findInPath.explicit_extension_resolves")
{
    // A command typed with an explicit PATHEXT extension is probed verbatim.
    platform::testing::InMemoryFileSystem fs;
    fs.addDirectory("/bin");
    fs.addExecutable("/bin/docker.exe");

    platform::TestEnvironmentProvider env;
    env.set("PATH", "/bin");
    env.set("PATHEXT", ".exe;.cmd;.bat");

    auto const resolver = CommandResolver(env, fs);
    auto const result = resolver.findInPath("docker.exe");
    REQUIRE(!result.empty());
    CHECK(result.ends_with("docker.exe"));
}

TEST_CASE("CommandResolver.findInPath.honors_PATHEXT_ordering")
{
    // When several PATHEXT variants exist, the one earliest in PATHEXT wins.
    platform::testing::InMemoryFileSystem fs;
    fs.addDirectory("/bin");
    fs.addExecutable("/bin/foo.cmd");
    fs.addExecutable("/bin/foo.exe");

    platform::TestEnvironmentProvider env;
    env.set("PATH", "/bin");
    env.set("PATHEXT", ".exe;.cmd");

    auto const resolver = CommandResolver(env, fs);
    auto const result = resolver.findInPath("foo");
    REQUIRE(!result.empty());
    CHECK(result.ends_with("foo.exe"));
}

TEST_CASE("CommandResolver.findInPath.typed_non_PATHEXT_extension_resolves_verbatim")
{
    // A name typed with an extension is used verbatim even when that extension is not in
    // PATHEXT (cmd.exe semantics: an explicit extension is never augmented). Without this
    // an explicitly typed "deploy.ps1" would be dropped when ".ps1" is absent from PATHEXT.
    platform::testing::InMemoryFileSystem fs;
    fs.addDirectory("/bin");
    fs.addExecutable("/bin/deploy.ps1");

    platform::TestEnvironmentProvider env;
    env.set("PATH", "/bin");
    env.set("PATHEXT", ".exe;.cmd"); // note: no .ps1

    auto const resolver = CommandResolver(env, fs);
    auto const result = resolver.findInPath("deploy.ps1");
    REQUIRE(!result.empty());
    CHECK(result.ends_with("deploy.ps1"));
}

TEST_CASE("CommandResolver.findInPath.empty_PATHEXT_falls_back_to_defaults")
{
    // An empty/blank PATHEXT must not strand bare lookups: fall back to the default
    // executable extensions so "docker" still resolves to "docker.exe".
    platform::testing::InMemoryFileSystem fs;
    fs.addDirectory("/bin");
    fs.addExecutable("/bin/docker.exe");

    platform::TestEnvironmentProvider env;
    env.set("PATH", "/bin");
    env.set("PATHEXT", "");

    auto const resolver = CommandResolver(env, fs);
    auto const result = resolver.findInPath("docker");
    REQUIRE(!result.empty());
    CHECK(result.ends_with("docker.exe"));
}

TEST_CASE("CommandResolver.findInPath.empty_PATHEXT_token_does_not_reshadow")
{
    // A stray empty token (e.g. ".exe;;.cmd") must be skipped — otherwise it would append
    // nothing and re-probe the bare name, letting the extensionless shim shadow docker.exe.
    platform::testing::InMemoryFileSystem fs;
    fs.addDirectory("/bin");
    fs.addExecutable("/bin/docker");
    fs.addExecutable("/bin/docker.exe");

    platform::TestEnvironmentProvider env;
    env.set("PATH", "/bin");
    env.set("PATHEXT", ".exe;;.cmd");

    auto const resolver = CommandResolver(env, fs);
    auto const result = resolver.findInPath("docker");
    REQUIRE(!result.empty());
    CHECK(result.ends_with("docker.exe"));
}

TEST_CASE("CommandResolver.findInPath.extensionless_does_not_shadow_later_directory")
{
    // The user's real scenario: an extensionless "docker" shim in an earlier PATH directory
    // must not shadow the real "docker.exe" in a later one.
    platform::testing::InMemoryFileSystem fs;
    fs.addDirectory("/a");
    fs.addDirectory("/b");
    fs.addExecutable("/a/docker"); // extensionless shim, earlier in PATH
    fs.addExecutable("/b/docker.exe");

    platform::TestEnvironmentProvider env;
    env.set("PATH", "/a;/b");
    env.set("PATHEXT", ".exe;.cmd;.bat");

    auto const resolver = CommandResolver(env, fs);
    auto const result = resolver.findInPath("docker");
    REQUIRE(!result.empty());
    CHECK(result.ends_with("docker.exe"));
}

TEST_CASE("CommandResolver.resolve.invalidates_cache_on_PATHEXT_change")
{
    // resolve() caches per command; since PATHEXT now governs whether anything is found,
    // a PATHEXT change (with PATH unchanged) must invalidate the cached result.
    platform::testing::InMemoryFileSystem fs;
    fs.addDirectory("/bin");
    fs.addExecutable("/bin/docker.exe");

    platform::TestEnvironmentProvider env;
    env.set("PATH", "/bin");
    env.set("PATHEXT", ".cmd"); // no .exe yet → docker.exe unreachable

    auto const resolver = CommandResolver(env, fs);
    CHECK(resolver.resolve("docker").type == CommandType::NotFound);

    env.set("PATHEXT", ".exe;.cmd"); // now docker.exe is reachable
    CHECK(resolver.resolve("docker").type == CommandType::External);
}
#endif

#if !defined(_WIN32)
TEST_CASE("CommandResolver.findInPath.skips_non_executable")
{
    // POSIX-only, even though the in-memory model carries an execute bit everywhere:
    // on Windows candidateNames() never probes the extensionless name (it expands "noexec"
    // to noexec.exe/.cmd/.bat/.com/.ps1), so the search would come back empty whatever the
    // execute bit says — and NativeFileSystem on Windows deliberately treats any existing
    // non-directory entry as executable, so the assertion would not describe the real
    // platform behaviour either.
    PathDir dir;
    // Create a file without execute permission — should not be found.
    dir.createNonExecutable("noexec");

    platform::TestEnvironmentProvider env;
    env.set("PATH", dir.path.string());

    auto const resolver = CommandResolver(env, dir.fs);
    auto const result = resolver.findInPath("noexec");
    CHECK(result.empty());
}
#endif

TEST_CASE("CommandResolver.findInPath.resolves_through_injected_filesystem")
{
    // Drives PATH search entirely through an injected InMemoryFileSystem — no disk access —
    // proving the resolver now relies on the FileSystem abstraction rather than std::filesystem.
    platform::testing::InMemoryFileSystem fs;
    fs.addDirectory("/bin");
    platform::TestEnvironmentProvider env;
#if defined(_WIN32)
    fs.addExecutable("/bin/tool.exe");
    env.set("PATH", "/bin");
    env.set("PATHEXT", ".exe;.cmd");
#else
    fs.addExecutable("/bin/tool");
    env.set("PATH", "/bin");
#endif

    auto const resolver = CommandResolver(env, fs);
    auto const result = resolver.findInPath("tool");
    REQUIRE(!result.empty());
    CHECK(std::filesystem::path(result).filename().string().starts_with("tool"));
}
