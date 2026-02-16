// SPDX-License-Identifier: Apache-2.0
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <shell/Error.hpp>
#include <shell/Platform.hpp>
#include <shell/Process.hpp>

#if defined(_WIN32)
    #include <windows.h>

    #include <fcntl.h>
    #include <tlhelp32.h>

namespace endo
{

namespace
{
    /// @brief Quotes a single argument for the Windows CreateProcess command line.
    ///
    /// Follows the Microsoft C/C++ parameter parsing rules:
    /// - Arguments with spaces, tabs, or quotes are enclosed in quotes
    /// - Backslashes before quotes are doubled
    /// - Trailing backslashes before the closing quote are doubled
    ///
    /// @param arg The argument to quote.
    /// @return The properly quoted argument.
    auto quoteArgument(std::string_view arg) -> std::string
    {
        // Check if quoting is needed
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
                // Escape backslashes at end (they'll be followed by closing quote)
                result.append(numBackslashes * 2, '\\');
                break;
            }
            else if (*it == '"')
            {
                // Escape backslashes + the quote itself
                result.append(numBackslashes * 2 + 1, '\\');
                result += '"';
            }
            else
            {
                // Backslashes not followed by quote are literal
                result.append(numBackslashes, '\\');
                result += *it;
            }
        }

        result += '"';
        return result;
    }

    /// @brief Builds a Windows command line string from program path and arguments.
    ///
    /// @param program The program path.
    /// @param arguments The argument list.
    /// @return The properly quoted command line string.
    auto buildCommandLine(std::filesystem::path const& program, std::vector<std::string> const& arguments)
        -> std::wstring
    {
        auto cmdLine = quoteArgument(program.string());
        for (auto const& arg: arguments)
        {
            cmdLine += ' ';
            cmdLine += quoteArgument(arg);
        }

        // Convert to wide string
        auto const size = MultiByteToWideChar(CP_UTF8, 0, cmdLine.c_str(), -1, nullptr, 0);
        auto wideCmdLine = std::wstring(static_cast<size_t>(size), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, cmdLine.c_str(), -1, wideCmdLine.data(), size);

        // Remove trailing null
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

std::expected<ProcessId, ShellError> WindowsProcessManager::spawn(SpawnConfig const& config)
{
    // Build command line
    auto cmdLine = buildCommandLine(config.program, config.arguments);

    // Set up STARTUPINFOW with handle redirection
    STARTUPINFOW si {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;

    // Map stdin/stdout/stderr handles
    // InvalidHandle means "use the standard handle"
    auto const mapHandle = [](NativeHandle fd, DWORD stdType) -> HANDLE {
        if (fd == INVALID_HANDLE_VALUE)
            return GetStdHandle(stdType);
        return fd;
    };

    si.hStdInput = mapHandle(config.stdinFd, STD_INPUT_HANDLE);
    si.hStdOutput = mapHandle(config.stdoutFd, STD_OUTPUT_HANDLE);
    si.hStdError = mapHandle(config.stderrFd, STD_ERROR_HANDLE);

    // Process creation flags
    DWORD flags = CREATE_UNICODE_ENVIRONMENT;
    if (config.processGroup.has_value())
        flags |= CREATE_NEW_PROCESS_GROUP;

    PROCESS_INFORMATION pi {};
    if (!CreateProcessW(nullptr,       // lpApplicationName (use cmdLine)
                        cmdLine.data(), // lpCommandLine (mutable!)
                        nullptr,        // lpProcessAttributes
                        nullptr,        // lpThreadAttributes
                        TRUE,           // bInheritHandles
                        flags,          // dwCreationFlags
                        nullptr,        // lpEnvironment (inherit)
                        nullptr,        // lpCurrentDirectory (inherit)
                        &si,            // lpStartupInfo
                        &pi))           // lpProcessInformation
    {
        auto const err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
            return std::unexpected(ShellError::ProgramNotFound);
        return std::unexpected(ShellError::ExecFailed);
    }

    // Store process handle for lifecycle management
    auto const pid = pi.dwProcessId;
    _processHandles[pid] = pi.hProcess;
    CloseHandle(pi.hThread); // Thread handle is not needed

    // Track process group membership
    if (config.processGroup.has_value())
        _groupMembers[*config.processGroup].push_back(pid);

    return pid;
}

std::expected<WaitResult, ShellError> WindowsProcessManager::wait(ProcessId pid, WaitFlags flags)
{
    auto const it = _processHandles.find(pid);
    if (it == _processHandles.end())
        return std::unexpected(ShellError::WaitFailed);

    auto const handle = it->second;
    auto const timeout = flags.test(WaitFlag::NoHang) ? 0 : INFINITE;

    auto const waitResult = WaitForSingleObject(handle, timeout);

    if (waitResult == WAIT_TIMEOUT)
    {
        // NoHang and process still running
        return WaitResult { .exitCode = 0, .signaled = false, .stopped = false, .signal = 0 };
    }

    if (waitResult != WAIT_OBJECT_0)
        return std::unexpected(ShellError::WaitFailed);

    // Process has terminated
    DWORD exitCode = 0;
    GetExitCodeProcess(handle, &exitCode);

    CloseHandle(handle);
    _processHandles.erase(it);

    return WaitResult { .exitCode = static_cast<int>(exitCode), .signaled = false, .stopped = false, .signal = 0 };
}

std::expected<std::optional<std::pair<ProcessId, WaitResult>>, ShellError> WindowsProcessManager::waitPgid(
    ProcessId pgid, WaitFlags flags)
{
    // Collect handles for all group members
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

    return std::unexpected(ShellError::WaitFailed);
}

std::expected<void, ShellError> WindowsProcessManager::sendSignal(ProcessId pid, int signal)
{
    if (signal == SIGINT)
    {
        // Send Ctrl+C to the process group
        if (!GenerateConsoleCtrlEvent(CTRL_C_EVENT, pid))
            return std::unexpected(ShellError::ExecutionFailed);
        return {};
    }

    if (signal == SIGTERM || signal == SIGKILL)
    {
        auto const it = _processHandles.find(pid);
        if (it == _processHandles.end())
            return std::unexpected(ShellError::ExecutionFailed);

        if (!TerminateProcess(it->second, 1))
            return std::unexpected(ShellError::ExecutionFailed);
        return {};
    }

    if (signal == SIGTSTP)
    {
        // Suspend all threads of the target process
        return suspendProcess(pid);
    }

    if (signal == SIGCONT)
    {
        // Resume all threads of the target process
        return resumeProcess(pid);
    }

    // Unsupported signal - no-op
    return {};
}

std::expected<ProcessId, ShellError> WindowsProcessManager::getForegroundPgrp(NativeHandle /*fd*/)
{
    // No terminal foreground group concept on Windows
    return static_cast<ProcessId>(GetCurrentProcessId());
}

std::expected<void, ShellError> WindowsProcessManager::setForegroundPgrp(NativeHandle /*fd*/, ProcessId /*pgid*/)
{
    // No-op on Windows
    return {};
}

std::expected<NativeHandle, ShellError> WindowsProcessManager::openFile(std::filesystem::path const& path,
                                                                        int flags,
                                                                        int /*mode*/)
{
    // Map POSIX open flags to Windows CreateFile parameters
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

    // Make handle inheritable for child processes
    SECURITY_ATTRIBUTES sa {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    auto const handle = CreateFileW(
        path.wstring().c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, creation, FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (handle == INVALID_HANDLE_VALUE)
        return std::unexpected(ShellError::IoError);

    // Handle O_APPEND by seeking to end
    if (flags & O_APPEND)
        SetFilePointer(handle, 0, nullptr, FILE_END);

    return handle;
}

std::expected<ProcessId, ShellError> WindowsProcessManager::createSession()
{
    // Windows doesn't have sessions. Return current process ID.
    return static_cast<ProcessId>(GetCurrentProcessId());
}

std::expected<void, ShellError> WindowsProcessManager::setProcessGroup(ProcessId pid, ProcessId pgid)
{
    // Record in internal group tracking
    _groupMembers[pgid].push_back(pid);
    return {};
}

std::expected<void, ShellError> WindowsProcessManager::duplicateFd(NativeHandle src, NativeHandle dst)
{
    // Map well-known handles to standard handle types
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
            return std::unexpected(ShellError::HandleDuplicationFailed);
        return {};
    }

    // General handle duplication
    HANDLE dupHandle = INVALID_HANDLE_VALUE;
    if (!DuplicateHandle(
            GetCurrentProcess(), src, GetCurrentProcess(), &dupHandle, 0, TRUE, DUPLICATE_SAME_ACCESS))
    {
        return std::unexpected(ShellError::HandleDuplicationFailed);
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
    // via PROC_THREAD_ATTRIBUTE_HANDLE_LIST or bInheritHandle.
    // This is a no-op for Windows.
}

auto WindowsProcessManager::suspendProcess(ProcessId pid) -> std::expected<void, ShellError>
{
    auto const snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return std::unexpected(ShellError::ExecutionFailed);

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

auto WindowsProcessManager::resumeProcess(ProcessId pid) -> std::expected<void, ShellError>
{
    auto const snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return std::unexpected(ShellError::ExecutionFailed);

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

} // namespace endo

#endif // _WIN32
