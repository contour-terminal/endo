// SPDX-License-Identifier: Apache-2.0
//
// Fails in one way that can raise a Windows dialog, chosen by its argument, and must be seen to EXIT
// rather than wait for a click. Driven by cmake/tests/TestWindowsDialogCanary.cmake, one run per way.
//
// It calls nothing to suppress anything: what it proves is that cmake/WindowsDialogs.cmake linked the
// suppression into an executable whose main() never asked for it, which is the case that hung.

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

#if defined(_WIN32)
    #include <Windows.h>
    #include <crtdbg.h>
#endif

namespace
{

/// Exit status meaning "this way of failing is not compiled into this build".
constexpr int NotExercised = 77;

/// Exit status meaning "the failure was handled and execution continued", distinct from any success.
constexpr int ContinuedAfterFailure = 3;

} // namespace

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        std::fputs(
            "usage: endo-windows-dialog-canary probe|assert|abort|invalid-parameter|access-violation\n",
            stderr);
        return 2;
    }
    auto const mode = std::string_view { argv[1] };

    // Reads what was installed before main() without failing: whether CRT assert reports go to a
    // file rather than a window, and whether the process asked for no fault dialog. It is how the
    // PRODUCT variant is watched both ways without raising the dialog it keeps for a user.
    if (mode == "probe")
    {
#if defined(_WIN32)
        auto const reportMode = _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_REPORT_MODE);
        auto const errorMode = GetErrorMode();
    #if defined(_DEBUG)
        constexpr auto DebugCrt = 1;
    #else
        constexpr auto DebugCrt = 0; // _CrtSetReportMode is a no-op here, so its answer means nothing
    #endif
        std::printf("debug-crt=%d assert-report-to-file=%d fault-dialog-suppressed=%d\n",
                    DebugCrt,
                    (reportMode & _CRTDBG_MODE_FILE) != 0 ? 1 : 0,
                    (errorMode & SEM_NOGPFAULTERRORBOX) != 0 ? 1 : 0);
        return 0;
#else
        return NotExercised;
#endif
    }

    std::fprintf(stderr, "windows-dialog-canary: failing by %s\n", argv[1]);
    std::fflush(stderr);

    if (mode == "assert")
    {
#if defined(NDEBUG)
        return NotExercised;
#else
        [[maybe_unused]] auto const canaryHolds = false;
        assert(canaryHolds && "windows-dialog-canary asserts on purpose");
        return ContinuedAfterFailure;
#endif
    }
    if (mode == "abort")
        std::abort();
    if (mode == "invalid-parameter")
    {
#if defined(_WIN32)
        // A null destination is an invalid parameter to the CRT: a dialog in a Debug CRT, Watson in a
        // Release one, unless a handler was installed.
        char* volatile destination = nullptr;
        if (strcpy_s(destination, 1, "x") != 0)
            return ContinuedAfterFailure;
        return 0;
#else
        return NotExercised;
#endif
    }
    if (mode == "access-violation")
    {
        int* volatile target = nullptr;
        *target = 1;
        return ContinuedAfterFailure;
    }
    std::fprintf(stderr, "windows-dialog-canary: unknown mode %s\n", argv[1]);
    return 2;
}
