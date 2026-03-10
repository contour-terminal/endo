// SPDX-License-Identifier: Apache-2.0

#include <shell/DirectoryConfig.hpp>
#include <shell/Shell.hpp>
#include <shell/TTY.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <ranges>

#include <platform/NativeFileSystem.hpp>
#include <platform/testing/TestEnvironmentProvider.hpp>

using namespace std::string_view_literals;

namespace fs = std::filesystem;

namespace
{

/// Silent diagnostic sink for tests — collects messages without printing.
endo::DiagnosticSink silentDiag()
{
    return [](std::string const&) {
    };
}

/// Returns the NativeFileSystem singleton for test use.
endo::FileSystem& testFs()
{
    return endo::NativeFileSystem::instance();
}

struct TestShell
{
    endo::TestPTY pty;
    endo::TestEnvironment env;
    int exitCode = -1;

    endo::Shell shell { pty, env };

    std::string_view output() const noexcept { return pty.output(); }

    TestShell()
    {
        if (auto const* path = std::getenv("PATH"))
            env.set("PATH", path);
        if (auto const* home = std::getenv("HOME"))
            env.set("HOME", home);
    }

    TestShell& operator()(std::string_view cmd)
    {
        exitCode = shell.execute(std::string(cmd));
        return *this;
    }
};

/// RAII temporary directory with optional .local-env.endo config file.
struct TempConfigDir
{
    fs::path dir;

    explicit TempConfigDir(std::string const& suffix, std::string const& configContent = "")
    {
        dir = fs::temp_directory_path() / ("endo-test-dirconfig-" + suffix);
        fs::remove_all(dir); // Clean up from previous runs
        fs::create_directories(dir);
        if (!configContent.empty())
        {
            auto ofs = std::ofstream(dir / ".local-env.endo");
            ofs << configContent;
        }
    }

    ~TempConfigDir() { fs::remove_all(dir); }

    TempConfigDir(TempConfigDir const&) = delete;
    TempConfigDir& operator=(TempConfigDir const&) = delete;
};

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
    auto const tmpDir = fs::temp_directory_path() / "endo-test-trust";
    fs::remove_all(tmpDir);
    fs::create_directories(tmpDir);

    endo::TestEnvironment env;
    env.set("HOME", tmpDir.string());

    // Create, set trust, save
    {
        auto store = endo::DirectoryConfigTrustStore(testFs(), env, silentDiag());
        store.setTrust("/projects/foo/.local-env.endo", "abc123", true);
        store.setTrust("/projects/bar/.local-env.endo", "def456", false);
    }

    // Reload and verify
    {
        auto store = endo::DirectoryConfigTrustStore(testFs(), env, silentDiag());
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

    fs::remove_all(tmpDir);
}

TEST_CASE("dirconfig.trust_store.hash_change_invalidates")
{
    auto const tmpDir = fs::temp_directory_path() / "endo-test-trust-hash";
    fs::remove_all(tmpDir);
    fs::create_directories(tmpDir);

    endo::TestEnvironment env;
    env.set("HOME", tmpDir.string());

    auto store = endo::DirectoryConfigTrustStore(testFs(), env, silentDiag());
    store.setTrust("/projects/foo/.local-env.endo", "original_hash", true);

    // Same hash -> trusted
    auto const trust = store.checkTrust("/projects/foo/.local-env.endo", "original_hash");
    REQUIRE(trust.has_value());
    CHECK(*trust == true);

    // Different hash -> unknown (requires re-approval)
    auto const trustChanged = store.checkTrust("/projects/foo/.local-env.endo", "new_hash");
    CHECK(!trustChanged.has_value());

    fs::remove_all(tmpDir);
}

TEST_CASE("dirconfig.trust_store.revoke")
{
    auto const tmpDir = fs::temp_directory_path() / "endo-test-trust-revoke";
    fs::remove_all(tmpDir);
    fs::create_directories(tmpDir);

    endo::TestEnvironment env;
    env.set("HOME", tmpDir.string());

    auto store = endo::DirectoryConfigTrustStore(testFs(), env, silentDiag());
    store.setTrust("/projects/foo/.local-env.endo", "hash1", true);
    CHECK(store.checkTrust("/projects/foo/.local-env.endo", "hash1").has_value());

    store.revokeTrust("/projects/foo/.local-env.endo");
    CHECK(!store.checkTrust("/projects/foo/.local-env.endo", "hash1").has_value());

    fs::remove_all(tmpDir);
}

// ============================================================================
// Config Discovery
// ============================================================================

TEST_CASE("dirconfig.discovery.untrusted_config_not_loaded")
{
    auto const tmpDir = TempConfigDir("discovery-untrusted", "let x = 42\n");

    TestShell ts;
    ts.env.set("HOME", fs::temp_directory_path().string());
    setCwd(ts.env, tmpDir.dir);

    auto mgr = endo::DirectoryConfigManager(ts.shell, testFs(), ts.env, silentDiag());
    // Config exists but is not trusted — should not be loaded
    mgr.onDirectoryChanged(tmpDir.dir.string());
    CHECK(mgr.activeScopes().empty());
    // Should have diagnostic about untrusted config
    CHECK(!mgr.diagnostics().empty());
}

TEST_CASE("dirconfig.discovery.no_config_file")
{
    auto const tmpDir = fs::temp_directory_path() / "endo-test-no-config";
    fs::remove_all(tmpDir);
    fs::create_directories(tmpDir);

    TestShell ts;
    ts.env.set("HOME", fs::temp_directory_path().string());
    setCwd(ts.env, tmpDir);

    auto mgr = endo::DirectoryConfigManager(ts.shell, testFs(), ts.env, silentDiag());
    mgr.onDirectoryChanged(tmpDir.string());
    CHECK(mgr.activeScopes().empty());
    CHECK(mgr.diagnostics().empty());

    fs::remove_all(tmpDir);
}

// ============================================================================
// Load / Unload Lifecycle
// ============================================================================

TEST_CASE("dirconfig.load.trusted_config_introduces_function")
{
    auto const tmpDir = TempConfigDir("load-func", "let greet name = print name\n");

    TestShell ts;
    ts.env.set("HOME", fs::temp_directory_path().string());
    setCwd(ts.env, tmpDir.dir);

    auto mgr = endo::DirectoryConfigManager(ts.shell, testFs(), ts.env, silentDiag());
    mgr.allowConfig(tmpDir.dir);

    // Verify function was loaded
    CHECK(ts.shell.fsharpState().functions.contains("greet"));
    REQUIRE(mgr.activeScopes().size() == 1);
    REQUIRE(mgr.activeScopes()[0].functions.size() == 1);
    CHECK(mgr.activeScopes()[0].functions[0] == "greet");
}

TEST_CASE("dirconfig.unload.removes_function_on_cd_away")
{
    auto const tmpDir = TempConfigDir("unload-func", "let greet name = print name\n");
    auto const otherDir = fs::temp_directory_path() / "endo-test-other";
    fs::remove_all(otherDir);
    fs::create_directories(otherDir);

    TestShell ts;
    ts.env.set("HOME", fs::temp_directory_path().string());
    setCwd(ts.env, tmpDir.dir);

    auto mgr = endo::DirectoryConfigManager(ts.shell, testFs(), ts.env, silentDiag());

    // Trust and load
    mgr.allowConfig(tmpDir.dir);
    REQUIRE(ts.shell.fsharpState().functions.contains("greet"));

    // cd to unrelated directory — function should be removed
    setCwd(ts.env, otherDir);
    mgr.onDirectoryChanged(otherDir.string());
    CHECK(!ts.shell.fsharpState().functions.contains("greet"));
    CHECK(mgr.activeScopes().empty());

    fs::remove_all(otherDir);
}

TEST_CASE("dirconfig.load.trusted_config_introduces_binding")
{
    auto const tmpDir = TempConfigDir("load-binding", "let project_name = \"my-project\"\n");

    TestShell ts;
    ts.env.set("HOME", fs::temp_directory_path().string());
    setCwd(ts.env, tmpDir.dir);

    auto mgr = endo::DirectoryConfigManager(ts.shell, testFs(), ts.env, silentDiag());
    mgr.allowConfig(tmpDir.dir);

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
    auto const tmpDir = TempConfigDir("unload-binding", "let project_name = \"my-project\"\n");
    auto const otherDir = fs::temp_directory_path() / "endo-test-other-bind";
    fs::remove_all(otherDir);
    fs::create_directories(otherDir);

    TestShell ts;
    ts.env.set("HOME", fs::temp_directory_path().string());
    setCwd(ts.env, tmpDir.dir);

    auto mgr = endo::DirectoryConfigManager(ts.shell, testFs(), ts.env, silentDiag());
    mgr.allowConfig(tmpDir.dir);

    auto const& bindings = ts.shell.fsharpState().valueBindings;
    REQUIRE(std::ranges::any_of(bindings, [](auto const& vb) { return vb.name == "project_name"; }));

    setCwd(ts.env, otherDir);
    mgr.onDirectoryChanged(otherDir.string());
    CHECK(!std::ranges::any_of(bindings, [](auto const& vb) { return vb.name == "project_name"; }));

    fs::remove_all(otherDir);
}

// ============================================================================
// Sibling Transition
// ============================================================================

TEST_CASE("dirconfig.sibling_transition.swaps_configs")
{
    auto const projectA = TempConfigDir("sibling-a", "let project_a = 1\n");
    auto const projectB = TempConfigDir("sibling-b", "let project_b = 2\n");

    TestShell ts;
    ts.env.set("HOME", fs::temp_directory_path().string());

    auto mgr = endo::DirectoryConfigManager(ts.shell, testFs(), ts.env, silentDiag());

    // Start in project A
    setCwd(ts.env, projectA.dir);
    mgr.allowConfig(projectA.dir);
    REQUIRE(std::ranges::any_of(ts.shell.fsharpState().valueBindings,
                                [](auto const& vb) { return vb.name == "project_a"; }));

    // Pre-trust B so onDirectoryChanged picks it up
    setCwd(ts.env, projectB.dir);
    mgr.allowConfig(projectB.dir);

    setCwd(ts.env, projectB.dir);
    mgr.onDirectoryChanged(projectB.dir.string());

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
    TestShell ts;
    ts("dirconfig list");
    CHECK(ts.exitCode == 0);
    CHECK(ts.output().find("No directory configs") != std::string_view::npos);
}

TEST_CASE("dirconfig.builtin.unknown_subcommand")
{
    TestShell ts;
    ts("dirconfig foobar");
    CHECK(ts.exitCode == 1);
}

// ============================================================================
// Diagnostics Capture
// ============================================================================

TEST_CASE("dirconfig.diagnostics.untrusted_message_captured")
{
    auto const tmpDir = TempConfigDir("diag-capture", "let x = 1\n");

    TestShell ts;
    ts.env.set("HOME", fs::temp_directory_path().string());
    setCwd(ts.env, tmpDir.dir);

    auto mgr = endo::DirectoryConfigManager(ts.shell, testFs(), ts.env, silentDiag());
    mgr.onDirectoryChanged(tmpDir.dir.string());

    // Should have captured diagnostic about untrusted config
    auto const& diags = mgr.diagnostics();
    REQUIRE(!diags.empty());
    CHECK(diags[0].find("not trusted") != std::string::npos);
}
