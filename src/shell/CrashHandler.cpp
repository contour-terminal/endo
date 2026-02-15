// SPDX-License-Identifier: Apache-2.0
#include "CrashHandler.hpp"

#if __has_include(<stacktrace>)
    #include <stacktrace>
    #include <string>
    #define ENDO_HAS_STACKTRACE 1
#endif

#if !defined(_WIN32)
    #include <cstdio>
    #include <cstdlib>
    #include <cstring>

    #include <sys/stat.h>

    #if !defined(ENDO_HAS_STACKTRACE)
        #include <execinfo.h>
    #endif
    #include <fcntl.h>
    #include <signal.h>
    #include <time.h>
    #include <unistd.h>
#endif

namespace endo
{

#if !defined(_WIN32)

namespace
{

    // File-scope state for async-signal-safe access from the handler.
    char crashDir[4096] = {};
    int crashDirLen = 0;
    char const* crashVersion = "unknown";
    bool crashInitialized = false;

    /// @brief Formats an integer as decimal into a buffer (async-signal-safe).
    /// @return Number of characters written.
    int formatInt(char* buf, long long value)
    {
        if (value == 0)
        {
            buf[0] = '0';
            buf[1] = '\0';
            return 1;
        }

        char tmp[24];
        int len = 0;
        auto negative = false;

        if (value < 0)
        {
            negative = true;
            value = -value;
        }

        while (value > 0)
        {
            tmp[len++] = static_cast<char>('0' + static_cast<int>(value % 10));
            value /= 10;
        }

        int pos = 0;
        if (negative)
            buf[pos++] = '-';

        for (int i = len - 1; i >= 0; --i)
            buf[pos++] = tmp[i];

        buf[pos] = '\0';
        return pos;
    }

    /// @brief Formats an address as hex into a buffer (async-signal-safe).
    /// @return Number of characters written.
    int formatHex(char* buf, unsigned long long addr)
    {
        if (addr == 0)
        {
            buf[0] = '0';
            buf[1] = '\0';
            return 1;
        }

        char tmp[20];
        int len = 0;

        while (addr > 0)
        {
            auto const digit = static_cast<int>(addr & 0xF);
            tmp[len++] = digit < 10 ? static_cast<char>('0' + digit) : static_cast<char>('a' + digit - 10);
            addr >>= 4;
        }

        int pos = 0;
        for (int i = len - 1; i >= 0; --i)
            buf[pos++] = tmp[i];
        buf[pos] = '\0';
        return pos;
    }

    /// @brief Writes a string to a file descriptor (async-signal-safe).
    void writeStr(int fd, char const* str)
    {
        if (!str)
            return;

        auto len = static_cast<size_t>(0);
        while (str[len])
            ++len;

        auto written = static_cast<size_t>(0);
        while (written < len)
        {
            auto const result = write(fd, str + written, len - written);
            if (result <= 0)
                break;
            written += static_cast<size_t>(result);
        }
    }

    /// @brief Returns the signal name for a signal number (async-signal-safe).
    char const* signalName(int sig)
    {
        switch (sig)
        {
            case SIGSEGV: return "SIGSEGV";
            case SIGABRT: return "SIGABRT";
            case SIGBUS: return "SIGBUS";
            case SIGILL: return "SIGILL";
            case SIGFPE: return "SIGFPE";
            default: return "UNKNOWN";
        }
    }

    /// @brief The signal handler (async-signal-safe).
    void crashSignalHandler(int sig, siginfo_t* info, void* /*ucontext*/)
    {
        if (!crashInitialized)
        {
            raise(sig);
            _exit(128 + sig);
        }

        // --- Build filename: crash-YYYY-MM-DD_HH-MM-SS_PID.log ---

        struct timespec ts {};
        clock_gettime(CLOCK_REALTIME, &ts);
        struct tm tm {};
        gmtime_r(&ts.tv_sec, &tm);

        char filename[4096 + 128];
        char* p = filename;

        for (int i = 0; i < crashDirLen; ++i)
            *p++ = crashDir[i];
        *p++ = '/';

        static char const prefix[] = "crash-";
        for (int i = 0; prefix[i]; ++i)
            *p++ = prefix[i];

        auto writeTwo = [](char*& out, int val) {
            *out++ = static_cast<char>('0' + (val / 10));
            *out++ = static_cast<char>('0' + (val % 10));
        };

        auto const year = tm.tm_year + 1900;
        *p++ = static_cast<char>('0' + (year / 1000));
        *p++ = static_cast<char>('0' + ((year / 100) % 10));
        *p++ = static_cast<char>('0' + ((year / 10) % 10));
        *p++ = static_cast<char>('0' + (year % 10));
        *p++ = '-';
        writeTwo(p, tm.tm_mon + 1);
        *p++ = '-';
        writeTwo(p, tm.tm_mday);
        *p++ = '_';
        writeTwo(p, tm.tm_hour);
        *p++ = '-';
        writeTwo(p, tm.tm_min);
        *p++ = '-';
        writeTwo(p, tm.tm_sec);

        *p++ = '_';
        p += formatInt(p, static_cast<long long>(getpid()));

        static char const suffix[] = ".log";
        for (int i = 0; suffix[i]; ++i)
            *p++ = suffix[i];
        *p = '\0';

        // --- Write crash log ---

        auto const fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (fd < 0)
        {
            raise(sig);
            _exit(128 + sig);
        }

        writeStr(fd, "=== Endo Shell Crash Report ===\n");

        writeStr(fd, "Version: ");
        writeStr(fd, crashVersion);
        writeStr(fd, "\n");

        writeStr(fd, "Signal: ");
        writeStr(fd, signalName(sig));
        writeStr(fd, " (");
        char numBuf[24];
        formatInt(numBuf, sig);
        writeStr(fd, numBuf);
        writeStr(fd, ")\n");

        if (info && (sig == SIGSEGV || sig == SIGBUS))
        {
            writeStr(fd, "Fault address: 0x");
            char hexBuf[20];
            formatHex(hexBuf, reinterpret_cast<unsigned long long>(info->si_addr));
            writeStr(fd, hexBuf);
            writeStr(fd, "\n");
        }

        writeStr(fd, "PID: ");
        formatInt(numBuf, static_cast<long long>(getpid()));
        writeStr(fd, numBuf);
        writeStr(fd, "\n");

        writeStr(fd, "Time: ");
        char timeBuf[64];
        char* tp = timeBuf;
        *tp++ = static_cast<char>('0' + (year / 1000));
        *tp++ = static_cast<char>('0' + ((year / 100) % 10));
        *tp++ = static_cast<char>('0' + ((year / 10) % 10));
        *tp++ = static_cast<char>('0' + (year % 10));
        *tp++ = '-';
        writeTwo(tp, tm.tm_mon + 1);
        *tp++ = '-';
        writeTwo(tp, tm.tm_mday);
        *tp++ = ' ';
        writeTwo(tp, tm.tm_hour);
        *tp++ = ':';
        writeTwo(tp, tm.tm_min);
        *tp++ = ':';
        writeTwo(tp, tm.tm_sec);
        *tp++ = ' ';
        *tp++ = 'U';
        *tp++ = 'T';
        *tp++ = 'C';
        *tp = '\0';
        writeStr(fd, timeBuf);
        writeStr(fd, "\n");

        writeStr(fd, "\nBacktrace:\n");
    #if defined(ENDO_HAS_STACKTRACE)
        // std::stacktrace::current() allocates, which is not async-signal-safe.
        // This is acceptable in a fatal crash handler — best-effort before termination.
        auto const trace = std::stacktrace::current();
        for (auto const& entry: trace)
        {
            auto const desc = std::to_string(entry);
            write(fd, desc.c_str(), desc.size());
            write(fd, "\n", 1);
        }
    #else
        void* frames[128];
        auto const frameCount = backtrace(frames, 128);
        backtrace_symbols_fd(frames, frameCount, fd);
    #endif

        writeStr(fd, "\n=== End of Crash Report ===\n");
        close(fd);

        // Brief message to stderr.
        writeStr(STDERR_FILENO, "\nendo: fatal signal ");
        writeStr(STDERR_FILENO, signalName(sig));
        writeStr(STDERR_FILENO, " — crash report written to ");
        writeStr(STDERR_FILENO, filename);
        writeStr(STDERR_FILENO, "\n");

        // Re-raise for default handler (core dump).
        // SA_RESETHAND already restored SIG_DFL.
        raise(sig);
        _exit(128 + sig);
    }

} // namespace

void CrashHandler::initialize(char const* version)
{
    crashVersion = version ? version : "unknown";

    auto const* home = std::getenv("HOME");
    if (!home)
        return;

    auto const written = std::snprintf(crashDir, sizeof(crashDir), "%s/.local/state/endo/crash", home);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(crashDir))
        return;

    crashDirLen = written;

    // Create directory hierarchy (mkdir -p equivalent).
    {
        char tmp[4096];
        std::strncpy(tmp, crashDir, sizeof(tmp));
        tmp[sizeof(tmp) - 1] = '\0';

        for (char* p = tmp + 1; *p; ++p)
        {
            if (*p == '/')
            {
                *p = '\0';
                mkdir(tmp, 0700);
                *p = '/';
            }
        }
        mkdir(tmp, 0700);
    }

    struct stat st {};
    if (stat(crashDir, &st) != 0 || !S_ISDIR(st.st_mode))
        return;

    // SA_SIGINFO:  provides siginfo_t* with faulting address.
    // SA_RESETHAND: restores default handler after first delivery (safe re-raise for core dump).
    struct sigaction sa {};
    sa.sa_sigaction = crashSignalHandler;
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGBUS, &sa, nullptr);
    sigaction(SIGILL, &sa, nullptr);
    sigaction(SIGFPE, &sa, nullptr);

    crashInitialized = true;
}

#else // _WIN32

    #if defined(ENDO_HAS_STACKTRACE)
        #include <cstdio>
        #include <filesystem>

        #include <dbghelp.h>
        #include <windows.h>

namespace
{

    char crashDirWin[4096] = {};
    char const* crashVersionWin = "unknown";

    /// @brief Writes a crash report using std::stacktrace on Windows.
    LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS* exInfo)
    {
        char filename[4096 + 128];
        SYSTEMTIME st;
        GetSystemTime(&st);

        std::snprintf(filename,
                      sizeof(filename),
                      "%s\\crash-%04d-%02d-%02d_%02d-%02d-%02d_%lu.log",
                      crashDirWin,
                      st.wYear,
                      st.wMonth,
                      st.wDay,
                      st.wHour,
                      st.wMinute,
                      st.wSecond,
                      GetCurrentProcessId());

        auto* f = std::fopen(filename, "w");
        if (!f)
            return EXCEPTION_CONTINUE_SEARCH;

        std::fprintf(f, "=== Endo Shell Crash Report ===\n");
        std::fprintf(f, "Version: %s\n", crashVersionWin);
        std::fprintf(f, "Exception code: 0x%08lX\n", exInfo ? exInfo->ExceptionRecord->ExceptionCode : 0UL);
        std::fprintf(f, "PID: %lu\n", GetCurrentProcessId());
        std::fprintf(f,
                     "Time: %04d-%02d-%02d %02d:%02d:%02d UTC\n",
                     st.wYear,
                     st.wMonth,
                     st.wDay,
                     st.wHour,
                     st.wMinute,
                     st.wSecond);

        std::fprintf(f, "\nBacktrace:\n");
        auto const trace = std::stacktrace::current();
        for (auto const& entry: trace)
        {
            auto const desc = std::to_string(entry);
            std::fprintf(f, "%s\n", desc.c_str());
        }

        std::fprintf(f, "\n=== End of Crash Report ===\n");
        std::fclose(f);

        std::fprintf(stderr, "\nendo: crash report written to %s\n", filename);
        return EXCEPTION_CONTINUE_SEARCH;
    }

} // namespace

void CrashHandler::initialize(char const* version)
{
    crashVersionWin = version ? version : "unknown";

    auto const* appdata = std::getenv("LOCALAPPDATA");
    if (!appdata)
        return;

    auto const written = std::snprintf(crashDirWin, sizeof(crashDirWin), "%s\\endo\\crash", appdata);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(crashDirWin))
        return;

    // Create directory hierarchy.
    std::filesystem::create_directories(crashDirWin);

    SetUnhandledExceptionFilter(unhandledExceptionFilter);
}

    #else

void CrashHandler::initialize(char const* /*version*/)
{
    // Windows without <stacktrace>: no-op.
}

    #endif

#endif

} // namespace endo
