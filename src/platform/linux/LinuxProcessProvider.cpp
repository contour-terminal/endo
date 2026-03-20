// SPDX-License-Identifier: Apache-2.0
#include "LinuxProcessProvider.hpp"
#include "ProcStatusParser.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#if !defined(_WIN32)
    #include <pwd.h>
    #include <unistd.h>
#endif

namespace endo::platform
{

namespace
{
    /// Reads the entire contents of a file into a string.
    /// Returns empty string on failure.
    std::string readFileContents(std::filesystem::path const& path)
    {
        std::ifstream file(path);
        if (!file)
            return {};
        std::ostringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }

    /// Resolves a numeric UID to a username via getpwuid_r.
    /// Returns the UID as a string if resolution fails.
    std::string uidToUsername([[maybe_unused]] int uid)
    {
#if !defined(_WIN32)
        struct passwd pwd {};
        struct passwd* result = nullptr;
        std::array<char, 1024> buf {};
        if (getpwuid_r(static_cast<uid_t>(uid), &pwd, buf.data(), buf.size(), &result) == 0 && result)
            return result->pw_name;
#endif
        return std::to_string(uid);
    }

    /// Reads the system uptime in seconds from /proc/uptime.
    double getUptimeSeconds()
    {
        auto const contents = readFileContents("/proc/uptime");
        if (contents.empty())
            return 0.0;
        double uptime = 0.0;
        std::istringstream iss(contents);
        iss >> uptime;
        return uptime;
    }

    /// Returns the number of clock ticks per second.
    long getClockTicksPerSecond()
    {
#if !defined(_WIN32)
        auto const ticks = sysconf(_SC_CLK_TCK);
        return ticks > 0 ? ticks : 100;
#else
        return 100;
#endif
    }
} // namespace

std::vector<ProcessEntry> LinuxProcessProvider::listProcesses() const
{
    std::vector<ProcessEntry> entries;
    auto const uptimeSeconds = getUptimeSeconds();
    auto const clockTicks = getClockTicksPerSecond();

    std::error_code ec;
    for (auto const& dirEntry: std::filesystem::directory_iterator("/proc", ec))
    {
        if (!dirEntry.is_directory(ec))
            continue;

        auto const filename = dirEntry.path().filename().string();
        if (filename.empty() || !std::ranges::all_of(filename, ::isdigit))
            continue;

        auto const& pidPath = dirEntry.path();

        auto const statContents = readFileContents(pidPath / "stat");
        if (statContents.empty())
            continue;

        ProcessEntry entry {};
        entry.pid = std::stoll(filename);

        auto const commEnd = statContents.rfind(')');
        if (commEnd == std::string::npos || commEnd + 2 >= statContents.size())
            continue;

        auto const commStart = statContents.find('(');
        if (commStart != std::string::npos && commStart < commEnd)
            entry.command = statContents.substr(commStart + 1, commEnd - commStart - 1);

        std::istringstream fields(statContents.substr(commEnd + 2));
        std::string state;
        int64_t ppid = 0;
        int64_t pgrp = 0;
        int64_t session = 0;
        int64_t ttyNr = 0;
        int64_t tpgid = 0;
        uint64_t flags = 0;
        uint64_t minflt = 0;
        uint64_t cminflt = 0;
        uint64_t majflt = 0;
        uint64_t cmajflt = 0;
        uint64_t utime = 0;
        uint64_t stime = 0;
        int64_t cutime = 0;
        int64_t cstime = 0;
        int64_t priority = 0;
        int64_t nice = 0;
        int64_t numThreads = 0;
        int64_t itrealvalue = 0;
        uint64_t starttime = 0;

        fields >> state >> ppid >> pgrp >> session >> ttyNr >> tpgid >> flags >> minflt >> cminflt >> majflt
            >> cmajflt >> utime >> stime >> cutime >> cstime >> priority >> nice >> numThreads >> itrealvalue
            >> starttime;

        entry.ppid = ppid;

        if (uptimeSeconds > 0.0)
        {
            auto const totalTimeSec = static_cast<double>(utime + stime) / static_cast<double>(clockTicks);
            auto const processUptimeSec =
                uptimeSeconds - (static_cast<double>(starttime) / static_cast<double>(clockTicks));
            if (processUptimeSec > 0.0)
                entry.cpuPercent = (totalTimeSec / processUptimeSec) * 100.0;
        }

        auto const statusContents = readFileContents(pidPath / "status");
        if (!statusContents.empty())
        {
            std::istringstream statusStream(statusContents);
            std::string line;
            while (std::getline(statusStream, line))
            {
                if (auto uid = parseUidFromStatusLine(line))
                    entry.user = uidToUsername(*uid);
                else if (auto rss = parseVmRssFromStatusLine(line))
                    entry.memKb = *rss;
            }
        }

        auto const cmdlineContents = readFileContents(pidPath / "cmdline");
        if (!cmdlineContents.empty())
        {
            std::string fullCmd;
            for (char c: cmdlineContents)
            {
                if (c == '\0')
                {
                    if (!fullCmd.empty())
                        break;
                }
                else
                {
                    fullCmd += c;
                }
            }
            if (!fullCmd.empty())
                entry.command = std::move(fullCmd);
        }

        entries.push_back(std::move(entry));
    }

    std::ranges::sort(entries, {}, &ProcessEntry::pid);
    return entries;
}

} // namespace endo::platform
