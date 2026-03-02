// SPDX-License-Identifier: Apache-2.0
#include "WindowsProcessProvider.hpp"

#if defined(_WIN32)

    #include <string>
    #include <vector>

    #include <windows.h>
    // windows.h must precede psapi.h and tlhelp32.h
    #include <psapi.h>
    #include <tlhelp32.h>

namespace endo::platform
{

std::vector<ProcessEntry> WindowsProcessProvider::listProcesses() const
{
    std::vector<ProcessEntry> result;

    HANDLE const snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return result;

    PROCESSENTRY32W pe {};
    pe.dwSize = sizeof(pe);

    if (!Process32FirstW(snapshot, &pe))
    {
        CloseHandle(snapshot);
        return result;
    }

    do
    {
        ProcessEntry entry {};
        entry.pid = static_cast<int64_t>(pe.th32ProcessID);
        entry.ppid = static_cast<int64_t>(pe.th32ParentProcessID);
        entry.cpuPercent = 0.0;

        std::string exeName;
        for (auto const* p = pe.szExeFile; *p != L'\0'; ++p)
            exeName += static_cast<char>(*p);
        entry.command = exeName;

        HANDLE const hProcess =
            OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
        if (hProcess != nullptr)
        {
            PROCESS_MEMORY_COUNTERS pmc {};
            pmc.cb = sizeof(pmc);
            if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc)))
                entry.memKb = static_cast<int64_t>(pmc.WorkingSetSize / 1024);

            HANDLE hToken = nullptr;
            if (OpenProcessToken(hProcess, TOKEN_QUERY, &hToken))
            {
                DWORD tokenInfoLen = 0;
                GetTokenInformation(hToken, TokenUser, nullptr, 0, &tokenInfoLen);

                if (tokenInfoLen > 0)
                {
                    auto tokenBuf = std::vector<char>(tokenInfoLen);
                    if (GetTokenInformation(hToken, TokenUser, tokenBuf.data(), tokenInfoLen, &tokenInfoLen))
                    {
                        auto const* tokenUser = reinterpret_cast<TOKEN_USER const*>(tokenBuf.data());
                        wchar_t userName[256] {};
                        DWORD userNameLen = 256;
                        wchar_t domainName[256] {};
                        DWORD domainLen = 256;
                        SID_NAME_USE sidType {};

                        if (LookupAccountSidW(nullptr,
                                              tokenUser->User.Sid,
                                              userName,
                                              &userNameLen,
                                              domainName,
                                              &domainLen,
                                              &sidType))
                        {
                            std::string narrowUser;
                            for (DWORD i = 0; i < userNameLen; ++i)
                                narrowUser += static_cast<char>(userName[i]);
                            entry.user = narrowUser;
                        }
                    }
                }
                CloseHandle(hToken);
            }

            CloseHandle(hProcess);
        }

        result.push_back(std::move(entry));
    } while (Process32NextW(snapshot, &pe));

    CloseHandle(snapshot);
    return result;
}

} // namespace endo::platform

#endif // _WIN32
