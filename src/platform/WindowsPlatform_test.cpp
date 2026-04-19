// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdlib>
#include <string_view>

#include <platform/PathUtils.hpp>
#include <platform/Types.hpp>
#include <platform/UserPaths.hpp>
#include <testing/EnvHelper.hpp>

#if defined(_WIN32)
    #include <windows.h>
#else
    #include <unistd.h>
#endif

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

TEST_CASE("resolveDevicePath.null_device", "[platform]")
{
    // `/dev/null` is mapped to the platform-native null device on Windows and
    // returned unchanged on POSIX systems.
#if defined(_WIN32)
    CHECK(resolveDevicePath("/dev/null") == "NUL");
#else
    CHECK(resolveDevicePath("/dev/null") == "/dev/null");
#endif
}

TEST_CASE("resolveDevicePath.regular_paths_unchanged", "[platform]")
{
    CHECK(resolveDevicePath("/tmp/foo.txt") == "/tmp/foo.txt");
    CHECK(resolveDevicePath("relative/path") == "relative/path");
    CHECK(resolveDevicePath("C:/Users/test/out.log") == "C:/Users/test/out.log");
    CHECK(resolveDevicePath("").empty());
}

TEST_CASE("resolveDevicePath.similar_but_not_null_device", "[platform]")
{
    // Only the exact POSIX null-device path is translated. Paths that merely start
    // with `/dev/` or contain `null` are regular filesystem paths.
    CHECK(resolveDevicePath("/dev/null/file") == "/dev/null/file");
    CHECK(resolveDevicePath("/dev/nullx") == "/dev/nullx");
    CHECK(resolveDevicePath("/dev/zero") == "/dev/zero");
    CHECK(resolveDevicePath("null") == "null");
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

TEST_CASE("platformRead.returns_zero_on_closed_pipe_eof", "[platform]")
{
    // Regression guard: on Windows, ReadFile on a drained pipe whose writer has
    // closed fails with ERROR_BROKEN_PIPE. platformRead must normalize that to
    // a POSIX-style EOF (return 0), not an I/O error (return -1).

#if defined(_WIN32)
    HANDLE readEnd = nullptr;
    HANDLE writeEnd = nullptr;
    REQUIRE(CreatePipe(&readEnd, &writeEnd, nullptr, 0) != 0);
    auto const readH = static_cast<NativeHandle>(readEnd);

    auto const payload = std::string_view { "hi" };
    DWORD written = 0;
    REQUIRE(WriteFile(writeEnd, payload.data(), static_cast<DWORD>(payload.size()), &written, nullptr) != 0);
    CHECK(written == payload.size());
    CloseHandle(writeEnd);
#else
    int fds[2] = { -1, -1 };
    REQUIRE(::pipe(fds) == 0);
    auto const readH = fds[0];
    auto const writeH = fds[1];

    auto const payload = std::string_view { "hi" };
    auto const expected = static_cast<intptr_t>(payload.size());
    auto const w = static_cast<intptr_t>(::write(writeH, payload.data(), payload.size()));
    REQUIRE(w == expected);
    ::close(writeH);
#endif

    std::array<char, 16> buf {};
    auto const first = platformRead(readH, buf.data(), buf.size());
    CHECK(first == 2);
    CHECK(std::string_view(buf.data(), static_cast<size_t>(first)) == "hi");

    // Second call: buffer empty, writer closed. Must be EOF, not error.
    auto const second = platformRead(readH, buf.data(), buf.size());
    CHECK(second == 0);

#if defined(_WIN32)
    CloseHandle(readEnd);
#else
    ::close(readH);
#endif
}
