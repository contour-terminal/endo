// SPDX-License-Identifier: Apache-2.0
//
// Installs testing::suppressWindowsDialogs() before main() runs, in an executable that links this.
//
// It is linked into EVERY executable target by cmake/WindowsDialogs.cmake rather than called from each
// main(), because a call each main() has to remember is the defect this closes: endo-test's main() had
// none, a .endo test tripped a CRT assert under ctest, and the modal dialog held the run for 58 minutes
// until somebody clicked it away. A header nobody includes fails silently.
//
// Built twice. As ENDO_WINDOWS_DIALOGS_ALWAYS it suppresses unconditionally, for test and tool
// executables. Without it, for a PRODUCT executable a user runs, it suppresses only when the process
// environment carries ENDO_SUPPRESS_WINDOWS_DIALOGS, which cmake/WindowsDialogs.cmake sets on every test
// ctest registers: an interactive user keeps the CRT's assert dialog and Windows Error Reporting exactly as
// before, and only a process started under ctest -- directly, or by a script a test runs -- reports to
// stderr and exits instead.

#if defined(_WIN32)

    #include <Windows.h>

    #include <testing/SuppressWindowsDialogs.hpp>

    #if defined(_MSC_VER)
        // Run in the library initialisation phase, ahead of every ordinary static initializer, so an
        // assert in one of those reports to stderr too.
        #pragma warning(disable : 4073)
        #pragma init_seg(lib)
    #endif

namespace
{

/// Whether this process should suppress: always, or only when started under ctest.
[[nodiscard]] bool suppressionRequested() noexcept
{
    #if defined(ENDO_WINDOWS_DIALOGS_ALWAYS)
    return true;
    #else
    // Presence alone decides; the value is not read. GetEnvironmentVariableW rather than getenv, which
    // the CRT flags as deprecated and which is not guaranteed usable this early in initialisation.
    return GetEnvironmentVariableW(L"ENDO_SUPPRESS_WINDOWS_DIALOGS", nullptr, 0) != 0;
    #endif
}

struct SuppressWindowsDialogsAtStartup
{
    SuppressWindowsDialogsAtStartup() noexcept
    {
        if (suppressionRequested())
            testing::suppressWindowsDialogs();
    }
};

SuppressWindowsDialogsAtStartup const suppressWindowsDialogsAtStartup {};

} // namespace

#endif
