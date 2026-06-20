// SPDX-License-Identifier: Apache-2.0
#include <platform/Process.hpp>

#if defined(_WIN32)
    #include <fcntl.h>
    #include <tlhelp32.h>
    #include <windows.h>

namespace endo::platform
{

namespace
{
    /// @brief Quotes a single argument for the Windows CreateProcess command line.
    auto quoteArgument(std::string_view arg) -> std::string
    {
        if (!arg.empty() && arg.find_first_of(" \t\"") == std::string_view::npos)
            return std::string(arg);

        std::string result;
        result.reserve(arg.size() + 4);
        result += '"';

        for (auto it = arg.begin(); it != arg.end(); ++it)
        {
            auto numBackslashes = 0u;
            while (it != arg.end() && *it == '\\')
            {
                ++it;
                ++numBackslashes;
            }

            if (it == arg.end())
            {
                result.append(numBackslashes * 2, '\\');
                break;
            }
            else if (*it == '"')
            {
                result.append(numBackslashes * 2 + 1, '\\');
                result += '"';
            }
            else
            {
                result.append(numBackslashes, '\\');
                result += *it;
            }
        }

        result += '"';
        return result;
    }

    /// @brief Builds a Windows command line string from program path and arguments.
    auto buildCommandLine(std::filesystem::path const& program, std::vector<std::string> const& arguments)
        -> std::wstring
    {
        auto cmdLine = quoteArgument(program.string());
        for (auto const& arg: arguments)
        {
            cmdLine += ' ';
            cmdLine += quoteArgument(arg);
        }

        auto const size = MultiByteToWideChar(CP_UTF8, 0, cmdLine.c_str(), -1, nullptr, 0);
        auto wideCmdLine = std::wstring(static_cast<size_t>(size), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, cmdLine.c_str(), -1, wideCmdLine.data(), size);

        if (!wideCmdLine.empty() && wideCmdLine.back() == L'\0')
            wideCmdLine.pop_back();

        return wideCmdLine;
    }
} // namespace

WindowsProcessManager& WindowsProcessManager::instance()
{
    static WindowsProcessManager pm;
    return pm;
}

std::expected<ProcessId, PlatformError> WindowsProcessManager::spawn(SpawnConfig const& config)
{
    auto cmdLine = buildCommandLine(config.program, config.arguments);

    STARTUPINFOW si {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;

    auto const mapHandle = [](NativeHandle fd, DWORD stdType) -> HANDLE {
        if (fd == INVALID_HANDLE_VALUE)
            return GetStdHandle(stdType);
        return fd;
    };

    si.hStdInput = mapHandle(config.stdinFd, STD_INPUT_HANDLE);
    si.hStdOutput = mapHandle(config.stdoutFd, STD_OUTPUT_HANDLE);
    si.hStdError = mapHandle(config.stderrFd, STD_ERROR_HANDLE);

    DWORD flags = CREATE_UNICODE_ENVIRONMENT;
    if (config.newConsoleProcessGroup)
        flags |= CREATE_NEW_PROCESS_GROUP;

    PROCESS_INFORMATION pi {};
    if (!CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, TRUE, flags, nullptr, nullptr, &si, &pi))
    {
        auto const err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
            return std::unexpected(PlatformError::ProgramNotFound);
        return std::unexpected(PlatformError::ExecFailed);
    }

    auto const pid = pi.dwProcessId;
    _processHandles[pid] = pi.hProcess;
    CloseHandle(pi.hThread);

    if (config.processGroup.has_value())
        _groupMembers[*config.processGroup].push_back(pid);

    return pid;
}

std::expected<WaitResult, PlatformError> WindowsProcessManager::wait(ProcessId pid, WaitFlags flags)
{
    auto const it = _processHandles.find(pid);
    if (it == _processHandles.end())
        return std::unexpected(PlatformError::WaitFailed);

    auto const handle = it->second;
    auto const timeout = flags.test(WaitFlag::NoHang) ? 0 : INFINITE;

    auto const waitResult = WaitForSingleObject(handle, timeout);

    if (waitResult == WAIT_TIMEOUT)
        return WaitResult { .exitCode = -1, .signaled = false, .stopped = false, .signal = 0 };

    if (waitResult != WAIT_OBJECT_0)
        return std::unexpected(PlatformError::WaitFailed);

    DWORD exitCode = 0;
    GetExitCodeProcess(handle, &exitCode);

    CloseHandle(handle);
    _processHandles.erase(it);

    return WaitResult {
        .exitCode = static_cast<int>(exitCode), .signaled = false, .stopped = false, .signal = 0
    };
}

std::expected<std::optional<std::pair<ProcessId, WaitResult>>, PlatformError> WindowsProcessManager::waitPgid(
    ProcessId pgid, WaitFlags flags)
{
    auto const groupIt = _groupMembers.find(pgid);
    if (groupIt == _groupMembers.end() || groupIt->second.empty())
        return std::nullopt;

    auto const& members = groupIt->second;
    std::vector<HANDLE> handles;
    std::vector<ProcessId> pids;

    for (auto const memberPid: members)
    {
        if (auto const handleIt = _processHandles.find(memberPid); handleIt != _processHandles.end())
        {
            handles.push_back(handleIt->second);
            pids.push_back(memberPid);
        }
    }

    if (handles.empty())
        return std::nullopt;

    auto const timeout = flags.test(WaitFlag::NoHang) ? 0 : INFINITE;
    auto const waitResult =
        WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), FALSE, timeout);

    if (waitResult == WAIT_TIMEOUT)
        return std::nullopt;

    if (waitResult >= WAIT_OBJECT_0 && waitResult < WAIT_OBJECT_0 + handles.size())
    {
        auto const idx = waitResult - WAIT_OBJECT_0;
        auto const changedPid = pids[idx];

        DWORD exitCode = 0;
        GetExitCodeProcess(handles[idx], &exitCode);
        CloseHandle(handles[idx]);
        _processHandles.erase(changedPid);

        return std::pair { changedPid,
                           WaitResult { .exitCode = static_cast<int>(exitCode),
                                        .signaled = false,
                                        .stopped = false,
                                        .signal = 0 } };
    }

    return std::unexpected(PlatformError::WaitFailed);
}

std::expected<void, PlatformError> WindowsProcessManager::sendSignal(ProcessId pid, int signal)
{
    if (signal == SIGINT)
    {
        // CTRL_C can only be delivered to a console process group. This succeeds for a
        // child created in its own group (CREATE_NEW_PROCESS_GROUP). A foreground child
        // that shares the shell's console group is not a group leader, so the event cannot
        // be targeted at it individually; fall back to terminating it by handle so callers
        // like `timeout --foreground -s INT` still stop the command.
        if (GenerateConsoleCtrlEvent(CTRL_C_EVENT, pid))
            return {};
        return terminateByHandle(pid);
    }

    if (signal == SIGTERM || signal == SIGKILL)
        return terminateByHandle(pid);

    if (signal == SIGTSTP)
        return suspendProcess(pid);

    if (signal == SIGCONT)
        return resumeProcess(pid);

    return {};
}

auto WindowsProcessManager::terminateByHandle(ProcessId pid) -> std::expected<void, PlatformError>
{
    auto const it = _processHandles.find(pid);
    if (it == _processHandles.end())
        return std::unexpected(PlatformError::SignalFailed);

    if (!TerminateProcess(it->second, 1))
        return std::unexpected(PlatformError::SignalFailed);
    return {};
}

std::expected<ProcessId, PlatformError> WindowsProcessManager::getForegroundPgrp(NativeHandle /*fd*/)
{
    return static_cast<ProcessId>(GetCurrentProcessId());
}

std::expected<void, PlatformError> WindowsProcessManager::setForegroundPgrp(NativeHandle /*fd*/,
                                                                            ProcessId /*pgid*/)
{
    return {};
}

std::expected<NativeHandle, PlatformError> WindowsProcessManager::openFile(std::filesystem::path const& path,
                                                                           int flags,
                                                                           int /*mode*/)
{
    DWORD access = 0;
    DWORD creation = OPEN_EXISTING;

    if ((flags & O_RDONLY) == O_RDONLY || (flags & O_RDWR) == O_RDWR)
        access |= GENERIC_READ;
    if ((flags & O_WRONLY) == O_WRONLY || (flags & O_RDWR) == O_RDWR)
        access |= GENERIC_WRITE;

    if (flags & O_CREAT)
    {
        if (flags & O_TRUNC)
            creation = CREATE_ALWAYS;
        else
            creation = OPEN_ALWAYS;
    }
    else if (flags & O_TRUNC)
    {
        creation = TRUNCATE_EXISTING;
    }

    SECURITY_ATTRIBUTES sa {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    // Special-case the POSIX /dev/null alias; pass any other path through unchanged
    // to preserve the original wide-string form (round-tripping through generic_string()
    // can lose non-ASCII characters for arbitrary Windows paths).
    auto const widePath = path.wstring();
    auto const* pathToOpen = (widePath == L"/dev/null") ? L"NUL" : widePath.c_str();
    auto const handle = CreateFileW(pathToOpen,
                                    access,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    &sa,
                                    creation,
                                    FILE_ATTRIBUTE_NORMAL,
                                    nullptr);

    if (handle == INVALID_HANDLE_VALUE)
        return std::unexpected(PlatformError::IoError);

    if (flags & O_APPEND)
        SetFilePointer(handle, 0, nullptr, FILE_END);

    return handle;
}

std::expected<ProcessId, PlatformError> WindowsProcessManager::createSession()
{
    return static_cast<ProcessId>(GetCurrentProcessId());
}

std::expected<void, PlatformError> WindowsProcessManager::setProcessGroup(ProcessId pid, ProcessId pgid)
{
    _groupMembers[pgid].push_back(pid);
    return {};
}

std::expected<void, PlatformError> WindowsProcessManager::duplicateFd(NativeHandle src, NativeHandle dst)
{
    auto const targetHandle = [](NativeHandle h) -> DWORD {
        auto const hStdin = GetStdHandle(STD_INPUT_HANDLE);
        auto const hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
        auto const hStderr = GetStdHandle(STD_ERROR_HANDLE);
        if (h == hStdin)
            return STD_INPUT_HANDLE;
        if (h == hStdout)
            return STD_OUTPUT_HANDLE;
        if (h == hStderr)
            return STD_ERROR_HANDLE;
        return static_cast<DWORD>(-1);
    };

    auto const stdHandleType = targetHandle(dst);
    if (stdHandleType != static_cast<DWORD>(-1))
    {
        if (!SetStdHandle(stdHandleType, src))
            return std::unexpected(PlatformError::HandleDuplicationFailed);
        return {};
    }

    HANDLE dupHandle = INVALID_HANDLE_VALUE;
    if (!DuplicateHandle(
            GetCurrentProcess(), src, GetCurrentProcess(), &dupHandle, 0, TRUE, DUPLICATE_SAME_ACCESS))
    {
        return std::unexpected(PlatformError::HandleDuplicationFailed);
    }

    return {};
}

void WindowsProcessManager::closeHandle(NativeHandle handle) noexcept
{
    if (handle != InvalidHandle)
        CloseHandle(handle);
}

void WindowsProcessManager::closeExtraHandles() noexcept
{
    // Windows handles are not inherited by default unless explicitly set
}

auto WindowsProcessManager::suspendProcess(ProcessId pid) -> std::expected<void, PlatformError>
{
    auto const snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return std::unexpected(PlatformError::SignalFailed);

    THREADENTRY32 te {};
    te.dwSize = sizeof(te);

    if (Thread32First(snapshot, &te))
    {
        do
        {
            if (te.th32OwnerProcessID == pid)
            {
                auto const hThread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
                if (hThread != nullptr)
                {
                    SuspendThread(hThread);
                    CloseHandle(hThread);
                }
            }
        } while (Thread32Next(snapshot, &te));
    }

    CloseHandle(snapshot);
    return {};
}

auto WindowsProcessManager::resumeProcess(ProcessId pid) -> std::expected<void, PlatformError>
{
    auto const snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return std::unexpected(PlatformError::SignalFailed);

    THREADENTRY32 te {};
    te.dwSize = sizeof(te);

    if (Thread32First(snapshot, &te))
    {
        do
        {
            if (te.th32OwnerProcessID == pid)
            {
                auto const hThread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
                if (hThread != nullptr)
                {
                    ResumeThread(hThread);
                    CloseHandle(hThread);
                }
            }
        } while (Thread32Next(snapshot, &te));
    }

    CloseHandle(snapshot);
    return {};
}

} // namespace endo::platform

#endif // _WIN32
