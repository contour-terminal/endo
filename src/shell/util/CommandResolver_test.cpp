// SPDX-License-Identifier: Apache-2.0

#include <shell/util/CommandResolver.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include <platform/testing/TestEnvironmentProvider.hpp>

using namespace endo;

namespace
{

/// @brief RAII helper that creates a temporary directory and removes it on destruction.
struct TempDir
{
    std::filesystem::path path;

    TempDir()
    {
        path = std::filesystem::temp_directory_path() / "endo_test_cmdresolver";
        std::filesystem::create_directories(path);
    }

    ~TempDir() { std::filesystem::remove_all(path); }

    /// @brief Creates a zero-byte file with the given name inside the temp directory.
    /// On POSIX, sets owner-execute permission.
    std::filesystem::path createExecutable(std::string const& name) const
    {
        auto const filePath = path / name;
        std::ofstream { filePath }.flush();
#if !defined(_WIN32)
        std::filesystem::permissions(
            filePath, std::filesystem::perms::owner_exec, std::filesystem::perm_options::add);
#endif
        return filePath;
    }

    /// @brief Creates a zero-byte file without execute permission (POSIX only distinction).
    std::filesystem::path createNonExecutable(std::string const& name) const
    {
        auto const filePath = path / name;
        std::ofstream { filePath }.flush();
        return filePath;
    }
};

} // namespace

TEST_CASE("CommandResolver.findInPath.bare_name_found")
{
    TempDir dir;
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

    auto const resolver = CommandResolver(env);
    auto const result = resolver.findInPath("testcmd");
    REQUIRE(!result.empty());
    CHECK(std::filesystem::path(result).filename().string().starts_with("testcmd"));
}

TEST_CASE("CommandResolver.findInPath.not_found")
{
    TempDir dir;

    platform::TestEnvironmentProvider env;
    env.set("PATH", dir.path.string());
#if defined(_WIN32)
    env.set("PATHEXT", ".exe");
#endif

    auto const resolver = CommandResolver(env);
    auto const result = resolver.findInPath("nonexistent");
    CHECK(result.empty());
}

TEST_CASE("CommandResolver.findInPath.missing_PATH")
{
    platform::TestEnvironmentProvider env;
    // No PATH set at all.

    auto const resolver = CommandResolver(env);
    auto const result = resolver.findInPath("anything");
    CHECK(result.empty());
}

TEST_CASE("CommandResolver.findInPath.skips_nonexistent_directory")
{
    TempDir dir;
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

    auto const resolver = CommandResolver(env);
    auto const result = resolver.findInPath("mycmd");
    REQUIRE(!result.empty());
    CHECK(std::filesystem::path(result).filename().string().starts_with("mycmd"));
}

#if defined(_WIN32)
TEST_CASE("CommandResolver.findInPath.PATHEXT_resolution")
{
    TempDir dir;
    // Create testapp.cmd — should be found when searching for "testapp"
    dir.createExecutable("testapp.cmd");

    platform::TestEnvironmentProvider env;
    env.set("PATH", dir.path.string());
    env.set("PATHEXT", ".exe;.cmd;.bat");

    auto const resolver = CommandResolver(env);
    auto const result = resolver.findInPath("testapp");
    REQUIRE(!result.empty());
    CHECK(result.ends_with(".cmd"));
}
#endif

#if !defined(_WIN32)
TEST_CASE("CommandResolver.findInPath.skips_non_executable")
{
    TempDir dir;
    // Create a file without execute permission — should not be found.
    dir.createNonExecutable("noexec");

    platform::TestEnvironmentProvider env;
    env.set("PATH", dir.path.string());

    auto const resolver = CommandResolver(env);
    auto const result = resolver.findInPath("noexec");
    CHECK(result.empty());
}
#endif
