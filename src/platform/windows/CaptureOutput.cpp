// SPDX-License-Identifier: Apache-2.0
#include <platform/CaptureOutput.hpp>
#include <platform/Pipe.hpp>
#include <platform/windows/CommandLine.hpp>
#include <platform/windows/HandleInheritance.hpp>

#if defined(_WIN32)
    #include <core/WideString.hpp>

    #include <array>
    #include <cstddef>
    #include <filesystem>
    #include <memory>
    #include <mutex>
    #include <optional>
    #include <ranges>
    #include <string>
    #include <vector>

    #include <windows.h>

namespace endo::platform
{

namespace
{
    using core::platform::PlatformError;

    /// Owns one kernel handle, and closes it.
    class ScopedHandle
    {
      public:
        explicit ScopedHandle(HANDLE handle) noexcept: _handle(handle) {}

        ~ScopedHandle()
        {
            if (valid())
                CloseHandle(_handle);
        }

        ScopedHandle(ScopedHandle const&) = delete;
        ScopedHandle& operator=(ScopedHandle const&) = delete;
        ScopedHandle(ScopedHandle&&) = delete;
        ScopedHandle& operator=(ScopedHandle&&) = delete;

        [[nodiscard]] HANDLE get() const noexcept { return _handle; }

        [[nodiscard]] bool valid() const noexcept
        {
            return _handle != nullptr && _handle != INVALID_HANDLE_VALUE;
        }

      private:
        HANDLE _handle;
    };

    /// A process-thread attribute list holding one attribute, deleted when it goes.
    class AttributeList
    {
      public:
        AttributeList()
        {
            auto size = SIZE_T { 0 };
            InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
            _storage.resize(size);
            _initialized = InitializeProcThreadAttributeList(get(), 1, 0, &size) != 0;
        }

        ~AttributeList()
        {
            if (_initialized)
                DeleteProcThreadAttributeList(get());
        }

        AttributeList(AttributeList const&) = delete;
        AttributeList& operator=(AttributeList const&) = delete;
        AttributeList(AttributeList&&) = delete;
        AttributeList& operator=(AttributeList&&) = delete;

        [[nodiscard]] bool initialized() const noexcept { return _initialized; }

        [[nodiscard]] LPPROC_THREAD_ATTRIBUTE_LIST get() noexcept
        {
            // An opaque type the API sizes and fills in; the storage is only its bytes.
            return reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(_storage.data());
        }

      private:
        std::vector<std::byte> _storage;
        bool _initialized = false;
    };

    /// @return Whether @p program names a program rather than a path to one.
    [[nodiscard]] bool isBareName(std::string_view program) noexcept
    {
        return program.find_first_of("\\/:") == std::string_view::npos;
    }

    /// @return The absolute directories PATH lists, joined as PATH joins them. A relative entry --
    ///         an empty one, or `.` -- names the current directory, which for an interactive shell
    ///         is wherever the user is, so it is left out.
    /// @param path The value of PATH.
    [[nodiscard]] auto absolutePathDirectories(std::wstring const& path) -> std::wstring
    {
        auto directories = std::wstring {};
        for (auto const entryRange: std::views::split(path, L';'))
        {
            auto entry = std::wstring(entryRange.begin(), entryRange.end());
            std::erase(entry, L'"');
            if (entry.empty() || !std::filesystem::path(entry).is_absolute())
                continue;
            if (!directories.empty())
                directories += L';';
            directories += entry;
        }
        return directories;
    }

    /// Finds the executable a bare name stands for in the absolute directories PATH lists, and
    /// nowhere else: `SearchPathW` given a path searches only it, where `CreateProcessW` given a
    /// bare name would search the current directory first.
    ///
    /// Only `.exe` is appended, not every PATHEXT extension: a `.cmd` or `.bat` runs only through
    /// `cmd.exe`, whose parsing of its command line is what running the program directly avoids.
    /// @param name The program's name; `.exe` is appended when it has no extension.
    /// @param environment Where PATH is read from.
    /// @return Its full path, or nothing when no such directory holds it.
    [[nodiscard]] auto findOnPath(std::wstring const& name, core::Environment const& environment)
        -> std::optional<std::wstring>
    {
        auto const path = environment.get("PATH");
        if (!path)
            return std::nullopt;
        auto const widePath = core::toUtf16(*path);
        if (!widePath)
            return std::nullopt;
        auto const directories = absolutePathDirectories(*widePath);
        if (directories.empty())
            return std::nullopt;
        auto const size = SearchPathW(directories.c_str(), name.c_str(), L".exe", 0, nullptr, nullptr);
        if (size == 0)
            return std::nullopt;
        auto found = std::wstring(size, L'\0');
        auto const written =
            SearchPathW(directories.c_str(), name.c_str(), L".exe", size, found.data(), nullptr);
        if (written == 0 || written >= size)
            return std::nullopt;
        found.resize(written);
        return found;
    }
} // namespace

auto captureOutput(std::string_view program,
                   std::span<std::string const> arguments,
                   core::Environment const& environment) -> std::expected<CapturedOutput, PlatformError>
{
    auto const wideProgram = core::toUtf16(program);
    if (!wideProgram)
        return std::unexpected(PlatformError::InvalidArgument);
    auto const executable =
        isBareName(program) ? findOnPath(*wideProgram, environment) : std::optional { *wideProgram };
    if (!executable)
        return std::unexpected(PlatformError::ProgramNotFound);

    auto commandLine = buildCommandLine(program, arguments);
    auto process = PROCESS_INFORMATION {};
    auto pipe = std::unique_ptr<Pipe> {};
    {
        // From the moment the child's handles exist until this process has closed its copies of
        // them, a job started meanwhile would inherit them; see handleInheritanceMutex.
        auto const lock = std::scoped_lock { handleInheritanceMutex() };

        auto created = createPipe();
        if (!created)
            return std::unexpected(created.error());
        pipe = std::move(*created);
        // Both ends are created inheritable, and only the write end is the child's.
        SetHandleInformation(pipe->reader(), HANDLE_FLAG_INHERIT, 0);

        auto inheritable = SECURITY_ATTRIBUTES { .nLength = sizeof(SECURITY_ATTRIBUTES),
                                                 .lpSecurityDescriptor = nullptr,
                                                 .bInheritHandle = TRUE };
        auto const nullDevice = ScopedHandle { CreateFileW(L"NUL",
                                                           GENERIC_READ | GENERIC_WRITE,
                                                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                           &inheritable,
                                                           OPEN_EXISTING,
                                                           0,
                                                           nullptr) };
        if (!nullDevice.valid())
            return std::unexpected(PlatformError::IoError);

        // These two handles reach the child, and no other inheritable handle this process holds: a
        // pipe end another thread is about to give its own child would otherwise be held open by
        // this one too, and that child's reader would not see its end until this child exited.
        auto inherited = std::array<HANDLE, 2> { pipe->writer(), nullDevice.get() };
        auto attributes = AttributeList {};
        if (!attributes.initialized()
            || UpdateProcThreadAttribute(attributes.get(),
                                         0,
                                         PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                         static_cast<void*>(inherited.data()),
                                         sizeof(HANDLE) * inherited.size(),
                                         nullptr,
                                         nullptr)
                   == 0)
            return std::unexpected(PlatformError::ExecFailed);

        auto startup = STARTUPINFOEXW {};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput = nullDevice.get();
        startup.StartupInfo.hStdOutput = pipe->writer();
        startup.StartupInfo.hStdError = nullDevice.get();
        startup.lpAttributeList = attributes.get();

        // CREATE_NO_WINDOW: a console of the child's own, without a window, rather than the
        // caller's. A child that attaches to a console whose host has died cannot initialize,
        // fails with 0xC0000142 and raises an error dialog -- which is what an orphaned shell's
        // prompt did.
        auto const started =
            CreateProcessW(executable->c_str(),
                           commandLine.data(),
                           nullptr,
                           nullptr,
                           TRUE,
                           CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT,
                           nullptr,
                           nullptr,
                           &startup.StartupInfo,
                           &process);
        auto const startError = GetLastError();
        // The child holds its own copy; this one would keep the read below from reaching the end.
        pipe->closeWriter();
        if (started == 0)
            return std::unexpected(startError == ERROR_FILE_NOT_FOUND || startError == ERROR_PATH_NOT_FOUND
                                       ? PlatformError::ProgramNotFound
                                       : PlatformError::ExecFailed);
    }
    auto const processHandle = ScopedHandle { process.hProcess };
    CloseHandle(process.hThread);

    // Until ReadFile fails, with ERROR_BROKEN_PIPE once every write end is closed: a read of zero
    // bytes that succeeds is a zero-length write by the child, not the end of its output.
    auto captured = CapturedOutput {};
    auto buffer = std::array<char, 4096> {};
    auto bytesRead = DWORD { 0 };
    while (ReadFile(pipe->reader(), buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr)
           != 0)
        captured.standardOutput.append(buffer.data(), bytesRead);

    auto exitCode = DWORD { 0 };
    if (WaitForSingleObject(processHandle.get(), INFINITE) != WAIT_OBJECT_0
        || GetExitCodeProcess(processHandle.get(), &exitCode) == 0)
        return std::unexpected(PlatformError::WaitFailed);
    captured.exitCode = static_cast<int>(exitCode);
    return captured;
}

} // namespace endo::platform

#endif // _WIN32
