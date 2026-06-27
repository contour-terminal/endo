// SPDX-License-Identifier: Apache-2.0
#include "DarwinProcessProvider.hpp"

#if defined(__APPLE__)

    #include <algorithm>
    #include <array>
    #include <ctime>
    #include <string>
    #include <vector>

    #include <sys/sysctl.h>

    #include <libproc.h>
    #include <pwd.h>
    #include <unistd.h>

namespace endo::platform
{

namespace
{
    std::string uidToUsername(uid_t uid)
    {
        struct passwd pwd {};
        struct passwd* result = nullptr;
        std::array<char, 1024> buf {};
        if (getpwuid_r(uid, &pwd, buf.data(), buf.size(), &result) == 0 && result)
            return result->pw_name;
        return std::to_string(uid);
    }

    double getSystemUptimeSeconds()
    {
        struct timeval boottime {};
        size_t size = sizeof(boottime);
        int mib[2] = { CTL_KERN, KERN_BOOTTIME };
        if (sysctl(mib, 2, &boottime, &size, nullptr, 0) == 0)
            return static_cast<double>(std::time(nullptr) - boottime.tv_sec);
        return 0.0;
    }
} // namespace

std::vector<ProcessEntry> DarwinProcessProvider::listProcesses() const
{
    // Step 1: Get all kinfo_proc entries via sysctl(KERN_PROC_ALL)
    int mib[3] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL };
    size_t bufSize = 0;
    if (sysctl(mib, 3, nullptr, &bufSize, nullptr, 0) != 0)
        return {};

    auto buffer = std::vector<char>(bufSize);
    if (sysctl(mib, 3, buffer.data(), &bufSize, nullptr, 0) != 0)
        return {};

    auto const count = bufSize / sizeof(struct kinfo_proc);
    auto const* procs = reinterpret_cast<struct kinfo_proc const*>(buffer.data());

    auto const uptimeSeconds = getSystemUptimeSeconds();

    std::vector<ProcessEntry> entries;
    entries.reserve(count);

    for (size_t i = 0; i < count; ++i)
    {
        auto const& kp = procs[i];
        auto const pid = static_cast<int64_t>(kp.kp_proc.p_pid);

        if (pid <= 0)
            continue;

        ProcessEntry entry {};
        entry.pid = pid;
        entry.ppid = static_cast<int64_t>(kp.kp_eproc.e_ppid);
        entry.user = uidToUsername(kp.kp_eproc.e_ucred.cr_uid);
        entry.command = kp.kp_proc.p_comm;

        // Step 2: Get task info (memory, CPU time) via proc_pidinfo
        struct proc_taskinfo taskInfo {};
        auto const taskInfoSize =
            proc_pidinfo(static_cast<int>(pid), PROC_PIDTASKINFO, 0, &taskInfo, sizeof(taskInfo));
        if (static_cast<size_t>(taskInfoSize) == sizeof(taskInfo))
        {
            entry.memKb = static_cast<int64_t>(taskInfo.pti_resident_size / 1024);

            if (uptimeSeconds > 0.0)
            {
                auto const totalCpuNs =
                    static_cast<double>(taskInfo.pti_total_user + taskInfo.pti_total_system);
                entry.cpuPercent = (totalCpuNs / (uptimeSeconds * 1e9)) * 100.0;
            }
        }

        entries.push_back(std::move(entry));
    }

    std::ranges::sort(entries, {}, &ProcessEntry::pid);
    return entries;
}

} // namespace endo::platform

#endif // __APPLE__
