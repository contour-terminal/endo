// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>

#include <platform/PathUtils.hpp>
#include <platform/UserPaths.hpp>
#include <testing/EnvHelper.hpp>

using namespace endo::platform;

TEST_CASE("normalizePath.forward_slashes_unchanged", "[platform]")
{
    CHECK(normalizePath(std::string("/usr/local/bin")) == "/usr/local/bin");
    CHECK(normalizePath(std::string("relative/path/file.txt")) == "relative/path/file.txt");
    CHECK(normalizePath(std::string("")).empty());
    CHECK(normalizePath(std::string("/")) == "/");
}

TEST_CASE("normalizePath.backslashes_converted", "[platform]")
{
    // On POSIX this is a no-op (backslashes are valid filename chars),
    // on Windows this converts backslashes to forward slashes.
#if defined(_WIN32)
    CHECK(normalizePath(std::string("C:\\Users\\test\\file.txt")) == "C:/Users/test/file.txt");
    CHECK(normalizePath(std::string("\\\\server\\share\\path")) == "//server/share/path");
    CHECK(normalizePath(std::string("mixed/path\\with\\both")) == "mixed/path/with/both");
    CHECK(normalizePath(std::string("C:\\")) == "C:/");
#else
    // On POSIX, backslashes are preserved (they're valid in filenames)
    CHECK(normalizePath(std::string("path\\with\\backslashes")) == "path\\with\\backslashes");
#endif
}

TEST_CASE("normalizePath.UNC_paths", "[platform]")
{
#if defined(_WIN32)
    CHECK(normalizePath(std::string("\\\\server\\share")) == "//server/share");
    CHECK(normalizePath(std::string("\\\\server\\share\\dir\\file")) == "//server/share/dir/file");
#endif
}

TEST_CASE("normalizePath.filesystem_path_overload", "[platform]")
{
    auto const p = std::filesystem::path("/some/path");
    CHECK(normalizePath(p) == "/some/path");
}

TEST_CASE("homeDirectory.returns_value_when_HOME_set", "[platform]")
{
    auto const* prevHome = std::getenv("HOME");
    auto const savedHome = prevHome ? std::string(prevHome) : std::string {};
    auto const hadHome = prevHome != nullptr;

    endo::testing::setTestEnv("HOME", "/tmp/test_home");
    auto const home = homeDirectory();
    REQUIRE(home.has_value());
    CHECK(*home == std::filesystem::path("/tmp/test_home"));

    if (hadHome)
        endo::testing::setTestEnv("HOME", savedHome.c_str());
    else
        endo::testing::unsetTestEnv("HOME");
}

TEST_CASE("homeDirectory.falls_back_to_USERPROFILE", "[platform]")
{
    auto const* prevHome = std::getenv("HOME");
    auto const savedHome = prevHome ? std::string(prevHome) : std::string {};
    auto const hadHome = prevHome != nullptr;

    auto const* prevProfile = std::getenv("USERPROFILE");
    auto const savedProfile = prevProfile ? std::string(prevProfile) : std::string {};
    auto const hadProfile = prevProfile != nullptr;

    endo::testing::unsetTestEnv("HOME");
    endo::testing::setTestEnv("USERPROFILE", "/tmp/test_profile");
    auto const home = homeDirectory();
    REQUIRE(home.has_value());
    CHECK(*home == std::filesystem::path("/tmp/test_profile"));

    if (hadHome)
        endo::testing::setTestEnv("HOME", savedHome.c_str());
    else
        endo::testing::unsetTestEnv("HOME");
    if (hadProfile)
        endo::testing::setTestEnv("USERPROFILE", savedProfile.c_str());
    else
        endo::testing::unsetTestEnv("USERPROFILE");
}

TEST_CASE("configHome.returns_XDG_CONFIG_HOME_when_set", "[platform]")
{
    auto const* prevXdg = std::getenv("XDG_CONFIG_HOME");
    auto const savedXdg = prevXdg ? std::string(prevXdg) : std::string {};
    auto const hadXdg = prevXdg != nullptr;

    endo::testing::setTestEnv("XDG_CONFIG_HOME", "/tmp/test_xdg_config");
    auto const config = configHome();
    REQUIRE(config.has_value());
    CHECK(*config == std::filesystem::path("/tmp/test_xdg_config"));

    if (hadXdg)
        endo::testing::setTestEnv("XDG_CONFIG_HOME", savedXdg.c_str());
    else
        endo::testing::unsetTestEnv("XDG_CONFIG_HOME");
}

TEST_CASE("configHome.falls_back_to_home_dot_config", "[platform]")
{
    auto const* prevXdg = std::getenv("XDG_CONFIG_HOME");
    auto const savedXdg = prevXdg ? std::string(prevXdg) : std::string {};
    auto const hadXdg = prevXdg != nullptr;

    auto const* prevAppdata = std::getenv("APPDATA");
    auto const savedAppdata = prevAppdata ? std::string(prevAppdata) : std::string {};
    auto const hadAppdata = prevAppdata != nullptr;

    auto const* prevHome = std::getenv("HOME");
    auto const savedHome = prevHome ? std::string(prevHome) : std::string {};
    auto const hadHome = prevHome != nullptr;

    endo::testing::unsetTestEnv("XDG_CONFIG_HOME");
    endo::testing::unsetTestEnv("APPDATA");
    endo::testing::setTestEnv("HOME", "/tmp/test_home");
    auto const config = configHome();
    REQUIRE(config.has_value());
    CHECK(*config == std::filesystem::path("/tmp/test_home/.config"));

    if (hadXdg)
        endo::testing::setTestEnv("XDG_CONFIG_HOME", savedXdg.c_str());
    else
        endo::testing::unsetTestEnv("XDG_CONFIG_HOME");
    if (hadAppdata)
        endo::testing::setTestEnv("APPDATA", savedAppdata.c_str());
    else
        endo::testing::unsetTestEnv("APPDATA");
    if (hadHome)
        endo::testing::setTestEnv("HOME", savedHome.c_str());
    else
        endo::testing::unsetTestEnv("HOME");
}
