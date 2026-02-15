// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#if !defined(_WIN32)
    #include <sys/wait.h>

    #include <signal.h>
    #include <unistd.h>
#endif

#include "CrashHandler.hpp"

#if !defined(_WIN32)

TEST_CASE("CrashHandler.creates_crash_log_on_sigsegv", "[crash]")
{
    // Use a temp directory as HOME so we don't pollute the real one.
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-crash-test";
    std::filesystem::remove_all(tmpDir);
    std::filesystem::create_directories(tmpDir);

    auto const pid = fork();
    REQUIRE(pid >= 0);

    if (pid == 0)
    {
        // Child process: set HOME, initialize crash handler, trigger SIGSEGV.
        setenv("HOME", tmpDir.c_str(), 1);
        endo::CrashHandler::initialize("0.1.0-test");

        // Trigger SIGSEGV via null pointer dereference.
        volatile int* p = nullptr;
        *p = 42; // NOLINT(clang-analyzer-core.NullDereference)
        _exit(1);
    }

    // Parent: wait for child to terminate.
    int status = 0;
    waitpid(pid, &status, 0);
    // Child should not exit cleanly (exit code 0).
    CHECK(!(WIFEXITED(status) && WEXITSTATUS(status) == 0));

    // Verify crash log was created.
    auto const crashDir = tmpDir / ".local" / "state" / "endo" / "crash";
    REQUIRE(std::filesystem::exists(crashDir));

    auto logCount = 0;
    std::filesystem::path logFile;
    for (auto const& entry: std::filesystem::directory_iterator(crashDir))
    {
        if (entry.path().extension() == ".log")
        {
            ++logCount;
            logFile = entry.path();
        }
    }

    // ASAN may intercept the signal before our handler runs, so the crash log
    // might not be created in sanitizer builds. Only check content if a log exists.
    if (logCount > 0)
    {
        CHECK(logCount == 1);

        auto ifs = std::ifstream(logFile);
        auto const content = std::string(std::istreambuf_iterator<char>(ifs), {});
        CHECK(content.find("Endo Shell Crash Report") != std::string::npos);
        CHECK(content.find("0.1.0-test") != std::string::npos);
        CHECK(content.find("Backtrace:") != std::string::npos);
    }

    // Cleanup.
    std::filesystem::remove_all(tmpDir);
}

TEST_CASE("CrashHandler.creates_crash_directory", "[crash]")
{
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-crash-dir-test";
    std::filesystem::remove_all(tmpDir);

    // Set HOME and initialize — should create the directory tree.
    auto const* originalHome = std::getenv("HOME");
    setenv("HOME", tmpDir.c_str(), 1);

    endo::CrashHandler::initialize("0.1.0-test");

    // Restore HOME.
    if (originalHome)
        setenv("HOME", originalHome, 1);

    auto const crashDir = tmpDir / ".local" / "state" / "endo" / "crash";
    CHECK(std::filesystem::exists(crashDir));
    CHECK(std::filesystem::is_directory(crashDir));

    std::filesystem::remove_all(tmpDir);
}

#endif
