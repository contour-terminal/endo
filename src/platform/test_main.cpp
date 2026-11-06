// SPDX-License-Identifier: Apache-2.0
#include <core/cli/App.hpp>
#include <core/log/LogStore.hpp>
#include <core/testing/SuppressWindowsDialogs.hpp>

#include <catch2/catch_session.hpp>

#include <charconv>
#include <cstdio>
#include <optional>
#include <span>
#include <string_view>

#if defined(_WIN32)
    #include <array>

    #include <windows.h>
#else
    #include <unistd.h>
#endif

namespace
{

/// @return Whether this process runs apart from its parent's terminal, as @c captureOutput promises
///         its child: alone on a console of its own on Windows, in a process group of its own on POSIX.
[[nodiscard]] bool runsApartFromParentTerminal()
{
#if defined(_WIN32)
    auto processes = std::array<DWORD, 8> {};
    return GetConsoleProcessList(processes.data(), static_cast<DWORD>(processes.size())) == 1;
#else
    return getpgrp() == getpid();
#endif
}

/// The child half of CaptureOutput_test, which starts this binary as the program it captures.
///
/// - `--endo-test-child <exit code> <arguments...>` writes each argument on a line of its own, as it
///   arrived, and exits with the code.
/// - `--endo-test-isolation` writes `apart` or `shared`; see @ref runsApartFromParentTerminal.
/// @param args This process's arguments.
/// @return The exit code to end with, or nothing when this is not a child's command line.
[[nodiscard]] std::optional<int> runAsCaptureChild(std::span<char const* const> args)
{
    if (args.size() >= 3 && std::string_view(args[1]) == "--endo-test-child")
    {
        auto const code = std::string_view(args[2]);
        auto exitCode = 0;
        std::from_chars(code.data(), code.data() + code.size(), exitCode);
        for (auto const* argument: args.subspan(3))
        {
            std::fputs(argument, stdout);
            std::fputc('\n', stdout);
        }
        return exitCode;
    }
    if (args.size() == 2 && std::string_view(args[1]) == "--endo-test-isolation")
    {
        std::fputs(runsApartFromParentTerminal() ? "apart\n" : "shared\n", stdout);
        return 0;
    }
    return std::nullopt;
}

} // namespace

int main(int argc, char const* argv[])
{
    if (auto const childExit = runAsCaptureChild(std::span(argv, static_cast<std::size_t>(argc))))
        return *childExit;

    core::testing::suppressWindowsDialogs();
    char const* logFilterString = getenv("LOG");
    if (logFilterString)
    {
        core::log::configure(logFilterString);
        core::cli::App::customizeLogStoreOutput();
    }
    int const result = Catch::Session().run(argc, argv);

    return result;
}
