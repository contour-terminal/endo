// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <platform/PathUtils.hpp>
#include <platform/UserPaths.hpp>

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
    // HOME is typically set on POSIX systems
#if !defined(_WIN32)
    auto const home = homeDirectory();
    REQUIRE(home.has_value());
    CHECK(!home->empty());
#endif
}

#if defined(_WIN32)
TEST_CASE("homeDirectory.returns_USERPROFILE_fallback", "[platform]")
{
    // On Windows, USERPROFILE should be available
    auto const home = homeDirectory();
    REQUIRE(home.has_value());
    CHECK(!home->empty());
}

TEST_CASE("configHome.returns_APPDATA_on_Windows", "[platform]")
{
    auto const config = configHome();
    REQUIRE(config.has_value());
    CHECK(!config->empty());
}
#endif

TEST_CASE("configHome.returns_value", "[platform]")
{
    // At minimum, configHome falls back to ~/.config when home is available
    auto const config = configHome();
    if (homeDirectory().has_value())
    {
        REQUIRE(config.has_value());
        CHECK(!config->empty());
    }
}
