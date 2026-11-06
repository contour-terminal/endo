// SPDX-License-Identifier: Apache-2.0
#include <core/testing/Environment.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <platform/CaptureOutput.hpp>

#if defined(_WIN32)
    #include <windows.h>
#elif defined(__APPLE__)
    #include <cstdint>

    #include <mach-o/dyld.h>
#endif

using core::platform::PlatformError;
using core::testing::FakeEnvironment;
using endo::platform::captureOutput;

namespace
{

#if defined(_WIN32)
constexpr auto PathSeparator = ';';
#else
constexpr auto PathSeparator = ':';
#endif

/// @return This test binary's own path. Its `main` doubles as the child the cases capture.
[[nodiscard]] std::filesystem::path selfPath()
{
#if defined(_WIN32)
    auto buffer = std::array<wchar_t, 32768> {};
    auto const length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    return std::filesystem::path(std::wstring_view(buffer.data(), length));
#elif defined(__APPLE__)
    auto size = std::uint32_t { 0 };
    _NSGetExecutablePath(nullptr, &size);
    auto buffer = std::string(size, '\0');
    _NSGetExecutablePath(buffer.data(), &size);
    return std::filesystem::canonical(buffer.c_str());
#else
    return std::filesystem::read_symlink("/proc/self/exe");
#endif
}

/// @return @p text split into lines, without the CR a Windows text-mode stdout adds to each.
[[nodiscard]] std::vector<std::string> linesOf(std::string text)
{
    std::erase(text, '\r');
    if (text.ends_with('\n'))
        text.pop_back();
    auto lines = std::vector<std::string> {};
    for (auto const line: std::views::split(text, '\n'))
        lines.emplace_back(line.begin(), line.end());
    return lines;
}

/// @return A small program of the system's that exits 0, to copy where PATH does not reach.
[[nodiscard]] std::filesystem::path systemProgramThatSucceeds()
{
#if defined(_WIN32)
    auto buffer = std::array<wchar_t, MAX_PATH> {};
    auto const length = GetSystemDirectoryW(buffer.data(), static_cast<UINT>(buffer.size()));
    return std::filesystem::path(std::wstring_view(buffer.data(), length)) / "hostname.exe";
#else
    for (auto const* candidate: { "/bin/true", "/usr/bin/true" })
        if (std::filesystem::exists(candidate))
            return candidate;
    return {};
#endif
}

/// A fresh directory holding a copy of @ref systemProgramThatSucceeds under a name nothing else has,
/// removed again with the case.
class ProbeDirectory
{
  public:
    static constexpr auto ProbeName = std::string_view { "endo-capture-probe" };

    ProbeDirectory(): _directory(std::filesystem::temp_directory_path() / "endo-capture-test")
    {
        std::filesystem::remove_all(_directory);
        std::filesystem::create_directories(_directory);
        auto const original = systemProgramThatSucceeds();
        if (original.empty() || !std::filesystem::exists(original))
            return;
        auto const copy = _directory / (std::string(ProbeName) + original.extension().string());
        std::filesystem::copy_file(original, copy);
        std::filesystem::permissions(
            copy, std::filesystem::perms::owner_exec, std::filesystem::perm_options::add);
        _probe = copy;
    }

    ~ProbeDirectory()
    {
        auto ignored = std::error_code {};
        std::filesystem::remove_all(_directory, ignored);
    }

    ProbeDirectory(ProbeDirectory const&) = delete;
    ProbeDirectory& operator=(ProbeDirectory const&) = delete;
    ProbeDirectory(ProbeDirectory&&) = delete;
    ProbeDirectory& operator=(ProbeDirectory&&) = delete;

    [[nodiscard]] std::filesystem::path const& path() const noexcept { return _directory; }

    /// @return The copied program, or nothing where the system has none to copy.
    [[nodiscard]] std::optional<std::filesystem::path> const& probe() const noexcept { return _probe; }

  private:
    std::filesystem::path _directory;
    std::optional<std::filesystem::path> _probe;
};

/// Makes @p directory the process's current one for the length of a case, then restores the
/// previous one.
class CurrentDirectoryScope
{
  public:
    explicit CurrentDirectoryScope(std::filesystem::path const& directory):
        _previous(std::filesystem::current_path())
    {
        std::filesystem::current_path(directory);
    }

    ~CurrentDirectoryScope()
    {
        auto ignored = std::error_code {};
        std::filesystem::current_path(_previous, ignored);
    }

    CurrentDirectoryScope(CurrentDirectoryScope const&) = delete;
    CurrentDirectoryScope& operator=(CurrentDirectoryScope const&) = delete;
    CurrentDirectoryScope(CurrentDirectoryScope&&) = delete;
    CurrentDirectoryScope& operator=(CurrentDirectoryScope&&) = delete;

  private:
    std::filesystem::path _previous;
};

} // namespace

TEST_CASE("captureOutput.passes_every_argument_unchanged_with_no_shell_between", "[platform][capture]")
{
    // What a directory name may hold: the prompt passes the shell's cwd as an argument, and a shell
    // in between would split it at a space and run what follows `&`, `;` or inside `$(...)`.
    auto const passed = std::vector<std::string> { "plain",
                                                   "with space",
                                                   "quote\"inside",
                                                   "amp&ersand calc",
                                                   "pipe|semi;colon",
                                                   "$(not expanded) `either`",
                                                   "%PATH%",
                                                   "trailing\\",
                                                   "",
                                                   R"(back\\slash "mix")" };
    auto arguments = std::vector<std::string> { "--endo-test-child", "0" };
    arguments.insert(arguments.end(), passed.begin(), passed.end());

    auto const captured = captureOutput(selfPath().string(), arguments, FakeEnvironment {});

    REQUIRE(captured.has_value());
    CHECK(captured->exitCode == 0);
    CHECK(linesOf(captured->standardOutput) == passed);
}

TEST_CASE("captureOutput.reports_the_exit_status", "[platform][capture]")
{
    auto const arguments = std::vector<std::string> { "--endo-test-child", "3" };

    auto const captured = captureOutput(selfPath().string(), arguments, FakeEnvironment {});

    REQUIRE(captured.has_value());
    CHECK(captured->exitCode == 3);
    CHECK(captured->standardOutput.empty());
}

TEST_CASE("captureOutput.runs_the_program_apart_from_the_callers_terminal", "[platform][capture]")
{
    // On Windows a console of its own: a child attached to its parent's console cannot start once that
    // console's host has died (0xC0000142, with an error dialog). On POSIX a process group of its own,
    // which the terminal's Ctrl+C and hangup do not reach.
    auto const arguments = std::vector<std::string> { "--endo-test-isolation" };

    auto const captured = captureOutput(selfPath().string(), arguments, FakeEnvironment {});

    REQUIRE(captured.has_value());
    CHECK(captured->standardOutput.starts_with("apart"));
}

TEST_CASE("captureOutput.a_bare_name_is_found_in_an_absolute_PATH_directory", "[platform][capture]")
{
    auto const directory = ProbeDirectory {};
    if (!directory.probe())
        SKIP("no small system program to copy");
    auto const environment = FakeEnvironment({ { "PATH", directory.path().string() } });

    auto const captured = captureOutput(ProbeDirectory::ProbeName, {}, environment);

    REQUIRE(captured.has_value());
    CHECK(captured->exitCode == 0);
}

TEST_CASE("captureOutput.a_program_not_on_PATH_is_not_found", "[platform][capture]")
{
    auto const directory = ProbeDirectory {};
    auto const environment = FakeEnvironment({ { "PATH", directory.path().string() } });

    auto const captured = captureOutput("endo-no-such-program-4f1b9c", {}, environment);

    REQUIRE_FALSE(captured.has_value());
    CHECK(captured.error() == PlatformError::ProgramNotFound);
}

TEST_CASE("captureOutput.a_bare_name_is_never_looked_up_in_the_current_directory", "[platform][capture]")
{
    // An interactive shell's current directory is wherever the user is, a cloned repository included,
    // so a `git` there must not be what the prompt runs -- not even through the PATH entries that
    // name it: an empty one (a leading, trailing or doubled separator), `.`, or a relative one.
    auto const directory = ProbeDirectory {};
    if (!directory.probe())
        SKIP("no small system program to copy");
    auto const scope = CurrentDirectoryScope { directory.path() };
    auto const elsewhere = std::filesystem::temp_directory_path().string(); // absolute, and no probe there
    auto path = std::string {};
    for (auto const& entry:
         { std::string {}, std::string("."), std::string("relative"), elsewhere, std::string {} })
    {
        if (!path.empty() || entry.empty())
            path += PathSeparator;
        path += entry;
    }
    auto const environment = FakeEnvironment({ { "PATH", path } });

    auto const byPath = captureOutput(directory.probe()->string(), {}, environment);
    auto const byName = captureOutput(ProbeDirectory::ProbeName, {}, environment);

    REQUIRE(byPath.has_value()); // the premise: the copy runs
    CHECK(byPath->exitCode == 0);
    INFO("PATH: " << path);
    REQUIRE_FALSE(byName.has_value());
    CHECK(byName.error() == PlatformError::ProgramNotFound);
}
