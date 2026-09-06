// SPDX-License-Identifier: Apache-2.0

#include <shell/DirectoryConfig.hpp>
#include <shell/Shell.hpp>
#include <shell/TTY.hpp>
#include <shell/testing/InjectedShell.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <memory>

#include <platform/testing/InMemoryFileSystem.hpp>
#include <platform/testing/TestEnvironmentProvider.hpp>

using namespace std::string_view_literals;

namespace fs = std::filesystem;

namespace
{
using endo::testing::InMemoryShell;

/// Silent diagnostic sink for tests — collects messages without printing.
endo::DiagnosticSink silentDiag()
{
    return [](std::string const&) {
    };
}

/// @brief Creates a directory under /test, optionally holding a .local-env.endo config.
/// @param fileSystem Filesystem to create it in.
/// @param name       Directory name below /test.
/// @param config     Config file contents; no file is written when empty.
/// @return The directory's path.
fs::path makeDir(endo::InMemoryFileSystem& fileSystem, std::string const& name, std::string_view config = {})
{
    auto const dir = fs::path("/test") / name;
    fileSystem.addDirectory(dir);
    if (!config.empty())
        fileSystem.addFile(dir / ".local-env.endo", std::string(config));
    return dir;
}

/// Helper: set the test environment CWD and register the path as valid.
void setCwd(endo::TestEnvironment& env, fs::path const& dir)
{
    env.addValidPath(dir.string());
    [[maybe_unused]] auto const result = env.changeDirectory(dir);
    env.set("PWD", dir.string());
}

} // namespace

// ============================================================================
// Trust Store
// ============================================================================

TEST_CASE("dirconfig.trust_store.round_trip")
{
    endo::InMemoryFileSystem fileSystem;
    endo::TestEnvironment env;
    env.set("HOME", "/test/home");

    // Create, set trust, save
    {
        auto store = endo::DirectoryConfigTrustStore(fileSystem, env, silentDiag());
        store.setTrust("/projects/foo/.local-env.endo", "abc123", true);
        store.setTrust("/projects/bar/.local-env.endo", "def456", false);
    }

    // Reload and verify
    {
        auto store = endo::DirectoryConfigTrustStore(fileSystem, env, silentDiag());
        store.load();

        auto const& entries = store.entries();
        REQUIRE(entries.size() == 2);

        auto const trust1 = store.checkTrust("/projects/foo/.local-env.endo", "abc123");
        REQUIRE(trust1.has_value());
        CHECK(*trust1 == true);

        auto const trust2 = store.checkTrust("/projects/bar/.local-env.endo", "def456");
        REQUIRE(trust2.has_value());
        CHECK(*trust2 == false);
    }
}

TEST_CASE("dirconfig.trust_store.hash_change_invalidates")
{
    endo::InMemoryFileSystem fileSystem;
    endo::TestEnvironment env;
    env.set("HOME", "/test/home");

    auto store = endo::DirectoryConfigTrustStore(fileSystem, env, silentDiag());
    store.setTrust("/projects/foo/.local-env.endo", "original_hash", true);

    // Same hash -> trusted
    auto const trust = store.checkTrust("/projects/foo/.local-env.endo", "original_hash");
    REQUIRE(trust.has_value());
    CHECK(*trust == true);

    // Different hash -> unknown (requires re-approval)
    auto const trustChanged = store.checkTrust("/projects/foo/.local-env.endo", "new_hash");
    CHECK(!trustChanged.has_value());
}

TEST_CASE("dirconfig.trust_store.revoke")
{
    endo::InMemoryFileSystem fileSystem;
    endo::TestEnvironment env;
    env.set("HOME", "/test/home");

    auto store = endo::DirectoryConfigTrustStore(fileSystem, env, silentDiag());
    store.setTrust("/projects/foo/.local-env.endo", "hash1", true);
    CHECK(store.checkTrust("/projects/foo/.local-env.endo", "hash1").has_value());

    store.revokeTrust("/projects/foo/.local-env.endo");
    CHECK(!store.checkTrust("/projects/foo/.local-env.endo", "hash1").has_value());
}

// ============================================================================
// Config Discovery
// ============================================================================

TEST_CASE("dirconfig.discovery.untrusted_config_not_loaded")
{
    InMemoryShell ts;
    auto const tmpDir = makeDir(ts.fs, "discovery-untrusted", "let x = 42\n");
    setCwd(ts.env, tmpDir);

    auto mgr = endo::DirectoryConfigManager(ts.shell, ts.fs, ts.env, silentDiag());
    // Config exists but is not trusted — should not be loaded
    mgr.onDirectoryChanged(tmpDir.string());
    CHECK(mgr.activeScopes().empty());
    // Should have diagnostic about untrusted config
    CHECK(!mgr.diagnostics().empty());
}

TEST_CASE("dirconfig.discovery.no_config_file")
{
    InMemoryShell ts;
    auto const tmpDir = makeDir(ts.fs, "no-config");
    setCwd(ts.env, tmpDir);

    auto mgr = endo::DirectoryConfigManager(ts.shell, ts.fs, ts.env, silentDiag());
    mgr.onDirectoryChanged(tmpDir.string());
    CHECK(mgr.activeScopes().empty());
    CHECK(mgr.diagnostics().empty());
}

// ============================================================================
// Load / Unload Lifecycle
// ============================================================================

TEST_CASE("dirconfig.load.trusted_config_introduces_function")
{
    InMemoryShell ts;
    auto const tmpDir = makeDir(ts.fs, "load-func", "let greet name = print name\n");
    setCwd(ts.env, tmpDir);

    auto mgr = endo::DirectoryConfigManager(ts.shell, ts.fs, ts.env, silentDiag());
    mgr.allowConfig(tmpDir);

    // Verify function was loaded
    CHECK(ts.shell.fsharpState().functions.contains("greet"));
    REQUIRE(mgr.activeScopes().size() == 1);
    REQUIRE(mgr.activeScopes()[0].functions.size() == 1);
    CHECK(mgr.activeScopes()[0].functions[0] == "greet");
}

TEST_CASE("dirconfig.unload.removes_function_on_cd_away")
{
    InMemoryShell ts;
    auto const tmpDir = makeDir(ts.fs, "unload-func", "let greet name = print name\n");
    auto const otherDir = makeDir(ts.fs, "otherDir");
    setCwd(ts.env, tmpDir);

    auto mgr = endo::DirectoryConfigManager(ts.shell, ts.fs, ts.env, silentDiag());

    // Trust and load
    mgr.allowConfig(tmpDir);
    REQUIRE(ts.shell.fsharpState().functions.contains("greet"));

    // cd to unrelated directory — function should be removed
    setCwd(ts.env, otherDir);
    mgr.onDirectoryChanged(otherDir.string());
    CHECK(!ts.shell.fsharpState().functions.contains("greet"));
    CHECK(mgr.activeScopes().empty());
}

TEST_CASE("dirconfig.load.trusted_config_introduces_binding")
{
    InMemoryShell ts;
    auto const tmpDir = makeDir(ts.fs, "load-binding", "let project_name = \"my-project\"\n");
    setCwd(ts.env, tmpDir);

    auto mgr = endo::DirectoryConfigManager(ts.shell, ts.fs, ts.env, silentDiag());
    mgr.allowConfig(tmpDir);

    // Verify binding was loaded
    auto const& bindings = ts.shell.fsharpState().valueBindings;
    auto const found =
        std::ranges::any_of(bindings, [](auto const& vb) { return vb.name == "project_name"; });
    CHECK(found);
    REQUIRE(mgr.activeScopes().size() == 1);
    CHECK(mgr.activeScopes()[0].bindings.size() == 1);
}

TEST_CASE("dirconfig.unload.removes_binding_on_cd_away")
{
    InMemoryShell ts;
    auto const tmpDir = makeDir(ts.fs, "unload-binding", "let project_name = \"my-project\"\n");
    auto const otherDir = makeDir(ts.fs, "otherDir");
    setCwd(ts.env, tmpDir);

    auto mgr = endo::DirectoryConfigManager(ts.shell, ts.fs, ts.env, silentDiag());
    mgr.allowConfig(tmpDir);

    auto const& bindings = ts.shell.fsharpState().valueBindings;
    REQUIRE(std::ranges::any_of(bindings, [](auto const& vb) { return vb.name == "project_name"; }));

    setCwd(ts.env, otherDir);
    mgr.onDirectoryChanged(otherDir.string());
    CHECK(!std::ranges::any_of(bindings, [](auto const& vb) { return vb.name == "project_name"; }));
}

// ============================================================================
// Sibling Transition
// ============================================================================

TEST_CASE("dirconfig.sibling_transition.swaps_configs")
{
    InMemoryShell ts;
    auto const projectA = makeDir(ts.fs, "sibling-a", "let project_a = 1\n");
    auto const projectB = makeDir(ts.fs, "sibling-b", "let project_b = 2\n");

    auto mgr = endo::DirectoryConfigManager(ts.shell, ts.fs, ts.env, silentDiag());

    // Start in project A
    setCwd(ts.env, projectA);
    mgr.allowConfig(projectA);
    REQUIRE(std::ranges::any_of(ts.shell.fsharpState().valueBindings,
                                [](auto const& vb) { return vb.name == "project_a"; }));

    // Pre-trust B so onDirectoryChanged picks it up
    setCwd(ts.env, projectB);
    mgr.allowConfig(projectB);

    setCwd(ts.env, projectB);
    mgr.onDirectoryChanged(projectB.string());

    // project_a should be gone, project_b should be present
    auto const& bindings = ts.shell.fsharpState().valueBindings;
    CHECK(!std::ranges::any_of(bindings, [](auto const& vb) { return vb.name == "project_a"; }));
    CHECK(std::ranges::any_of(bindings, [](auto const& vb) { return vb.name == "project_b"; }));
}

// ============================================================================
// Dirconfig Builtins
// ============================================================================

TEST_CASE("dirconfig.builtin.list_empty")
{
    InMemoryShell ts;
    ts("dirconfig list");
    CHECK(ts.exitCode == 0);
    CHECK(ts.output().find("No directory configs") != std::string_view::npos);
}

TEST_CASE("dirconfig.builtin.unknown_subcommand")
{
    InMemoryShell ts;
    ts("dirconfig foobar");
    CHECK(ts.exitCode == 1);
}

// ============================================================================
// Diagnostics Capture
// ============================================================================

TEST_CASE("dirconfig.diagnostics.untrusted_message_captured")
{
    InMemoryShell ts;
    auto const tmpDir = makeDir(ts.fs, "diag-capture", "let x = 1\n");
    setCwd(ts.env, tmpDir);

    auto mgr = endo::DirectoryConfigManager(ts.shell, ts.fs, ts.env, silentDiag());
    mgr.onDirectoryChanged(tmpDir.string());

    // Should have captured diagnostic about untrusted config
    auto const& diags = mgr.diagnostics();
    REQUIRE(!diags.empty());
    CHECK(diags[0].find("not trusted") != std::string::npos);
}
