// SPDX-License-Identifier: Apache-2.0
#include <platform/InstallPaths.hpp>
#include <platform/PathUtils.hpp>

#if defined(__APPLE__)
    #include <mach-o/dyld.h>
#elif defined(_WIN32)
    #include <windows.h>
#endif

#include <string>

namespace endo::platform
{

auto executablePath() -> std::optional<std::filesystem::path>
{
#if defined(__linux__)
    std::error_code ec;
    auto path = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec)
        return std::nullopt;
    return path;
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    auto buf = std::string(size, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) != 0)
        return std::nullopt;
    std::error_code ec;
    auto path = std::filesystem::canonical(buf, ec);
    if (ec)
        return std::nullopt;
    return path;
#elif defined(_WIN32)
    wchar_t buf[MAX_PATH];
    auto const len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
        return std::nullopt;
    return std::filesystem::path(normalizePath(std::filesystem::path(std::wstring_view(buf, len))));
#else
    return std::nullopt;
#endif
}

auto resolveDataDir(std::string_view subdir) -> std::filesystem::path
{
    auto const exePath = executablePath();
    if (!exePath)
        return {};
    // <prefix>/bin/endo → <prefix>/share/endo/<subdir>
    return exePath->parent_path().parent_path() / "share" / "endo" / subdir;
}

} // namespace endo::platform
