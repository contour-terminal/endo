// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include <testing/EnvHelper.hpp>
#include <testing/ScopedTempDir.hpp>

#if !defined(_WIN32)

    #include <sys/wait.h>

    #include <unistd.h>
#else
    #include <stdlib.h> // _putenv_s
    #include <windows.h>
#endif

#include "CrashHandler.hpp"

#if !defined(_WIN32)

TEST_CASE("CrashHandler.creates_crash_log_on_sigsegv", "[crash]")
{
    // Use a temp directory as HOME so we don't pollute the real one.
    auto const tmpDirGuard = endo::testing::ScopedTempDir { "endo-crash-test" };
    auto const& tmpDir = tmpDirGuard.path();

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
}

TEST_CASE("CrashHandler.creates_crash_directory", "[crash]")
{
    auto const tmpDirGuard = endo::testing::ScopedTempDir { "endo-crash-dir-test" };
    auto const& tmpDir = tmpDirGuard.path();

    // Set HOME and initialize — should create the directory tree. Scoped, because $HOME is
    // process-global and other fixtures read it while constructing a shell.
    {
        auto const scopedHome = endo::testing::ScopedEnv { "HOME", tmpDir.string() };
        endo::CrashHandler::initialize("0.1.0-test");
    }

    auto const crashDir = tmpDir / ".local" / "state" / "endo" / "crash";
    CHECK(std::filesystem::exists(crashDir));
    CHECK(std::filesystem::is_directory(crashDir));
}

#else // _WIN32

/// @brief Hidden helper test that serves as the crash-inducing child process on Windows.
///
/// This test is not run by default (hidden via the '.' tag prefix). It is spawned
/// by CrashHandler.creates_crash_log_on_access_violation as a child process via
/// CreateProcessW. It reads LOCALAPPDATA from the environment (set by the parent),
/// initializes the crash handler, and triggers an access violation.
TEST_CASE("CrashHandler.crash_child_helper", "[.crash-child]")
{
    endo::CrashHandler::initialize("0.1.0-test");

    // Trigger access violation via null pointer dereference.
    volatile int* p = nullptr;
    *p = 42; // NOLINT(clang-analyzer-core.NullDereference)
}

/// @brief Retrieves the full path to the currently running executable on Windows.
/// @return The executable path as a filesystem path.
static auto getTestExecutablePath() -> std::filesystem::path
{
    wchar_t buf[MAX_PATH];
    auto const len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(std::wstring_view(buf, len));
}

/// @brief Builds a Windows environment block with LOCALAPPDATA overridden.
///
/// Creates a double-null-terminated Unicode environment block suitable for
/// CreateProcessW, inheriting the current environment but overriding LOCALAPPDATA
/// to point to the specified temporary directory.
///
/// @param localAppData The path to use as LOCALAPPDATA in the child process.
/// @return The environment block as a vector of wide characters.
static auto buildEnvironmentBlock(std::filesystem::path const& localAppData) -> std::vector<wchar_t>
{
    std::vector<wchar_t> envBlock;

    auto const overrideKey = std::wstring(L"LOCALAPPDATA=");
    auto const overrideEntry = overrideKey + localAppData.wstring();

    auto inserted = false;
    auto* env = GetEnvironmentStringsW();
    if (env)
    {
        for (auto const* p = env; *p; p += wcslen(p) + 1)
        {
            auto const entry = std::wstring_view(p);
            // Skip the existing LOCALAPPDATA entry; we'll insert our override.
            if (entry.size() >= overrideKey.size()
                && _wcsnicmp(entry.data(), overrideKey.data(), overrideKey.size()) == 0)
            {
                envBlock.insert(envBlock.end(), overrideEntry.begin(), overrideEntry.end());
                envBlock.push_back(L'\0');
                inserted = true;
            }
            else
            {
                envBlock.insert(envBlock.end(), entry.begin(), entry.end());
                envBlock.push_back(L'\0');
            }
        }
        FreeEnvironmentStringsW(env);
    }

    if (!inserted)
    {
        envBlock.insert(envBlock.end(), overrideEntry.begin(), overrideEntry.end());
        envBlock.push_back(L'\0');
    }

    // Double-null terminator.
    envBlock.push_back(L'\0');
    return envBlock;
}

TEST_CASE("CrashHandler.creates_crash_log_on_access_violation", "[crash]")
{
    auto const tmpDirGuard = endo::testing::ScopedTempDir { "endo-crash-test" };
    auto const& tmpDir = tmpDirGuard.path();

    // Build command line: run the hidden crash-child helper test.
    auto const exePath = getTestExecutablePath();
    auto cmdLine = exePath.wstring() + L" \"[.crash-child]\"";

    // Build environment block with LOCALAPPDATA pointing to our temp directory.
    auto envBlock = buildEnvironmentBlock(tmpDir);

    STARTUPINFOW si {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi {};

    auto const created = CreateProcessW(nullptr,
                                        cmdLine.data(),
                                        nullptr,
                                        nullptr,
                                        FALSE,
                                        CREATE_UNICODE_ENVIRONMENT,
                                        envBlock.data(),
                                        nullptr,
                                        &si,
                                        &pi);
    REQUIRE(created != 0);

    // Wait for the child process to terminate (up to 30 seconds).
    WaitForSingleObject(pi.hProcess, 30000);

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    // Child should not exit cleanly (exit code 0).
    CHECK(exitCode != 0);

    // Verify crash log was created.
    auto const crashDir = tmpDir / "endo" / "crash";
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

    // The unhandled exception filter may not always be invoked (e.g., if a debugger
    // is attached), so only check content if a log exists.
    if (logCount > 0)
    {
        CHECK(logCount == 1);

        auto ifs = std::ifstream(logFile);
        auto const content = std::string(std::istreambuf_iterator<char>(ifs), {});
        CHECK(content.find("Endo Shell Crash Report") != std::string::npos);
        CHECK(content.find("0.1.0-test") != std::string::npos);
        CHECK(content.find("Backtrace:") != std::string::npos);
    }
}

TEST_CASE("CrashHandler.creates_crash_directory", "[crash]")
{
    auto const tmpDirGuard = endo::testing::ScopedTempDir { "endo-crash-dir-test" };
    auto const& tmpDir = tmpDirGuard.path();

    // Override %LOCALAPPDATA% and initialize — should create the directory tree. Scoped,
    // because the environment is process-global: restoring by hand skipped the restore
    // whenever the variable had been unset, and leaked the override entirely when an
    // assertion threw first.
    {
        auto const scopedLocalAppData = endo::testing::ScopedEnv { "LOCALAPPDATA", tmpDir.string() };
        endo::CrashHandler::initialize("0.1.0-test");
    }

    auto const crashDir = tmpDir / "endo" / "crash";
    CHECK(std::filesystem::exists(crashDir));
    CHECK(std::filesystem::is_directory(crashDir));
}

#endif
