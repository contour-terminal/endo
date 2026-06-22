// SPDX-License-Identifier: Apache-2.0
#include <platform/PathUtils.hpp>

#if defined(_WIN32)
    #include <cwctype>
    #include <memory>
    #include <string>
    #include <string_view>
    #include <type_traits>

    #include <windows.h>
#endif

namespace endo::platform
{

auto canonicalCasePath(std::filesystem::path const& p) -> std::string
{
#if defined(_WIN32)
    auto const native = p.wstring();
    if (native.empty())
        return normalizePath(p);

    // Open a handle to the path so its canonical, correctly-cased name can be queried.
    // GetLongPathNameW only expands 8.3 short names — it leaves already-long components
    // in whatever case they were passed — so it cannot fix the casing the shell needs.
    // FILE_FLAG_BACKUP_SEMANTICS is required to obtain a handle to a directory.
    auto* const rawHandle = CreateFileW(native.c_str(),
                                        0,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                        nullptr,
                                        OPEN_EXISTING,
                                        FILE_FLAG_BACKUP_SEMANTICS,
                                        nullptr);
    if (rawHandle == INVALID_HANDLE_VALUE)
        return normalizePath(p); // Non-existent path or access failure: fall back.

    // Own the handle via RAII so it is closed on every return path (including exceptions).
    auto const handle =
        std::unique_ptr<std::remove_pointer_t<HANDLE>, decltype(&CloseHandle)>(rawHandle, &CloseHandle);

    auto const flags = FILE_NAME_NORMALIZED | VOLUME_NAME_DOS;
    auto const needed = GetFinalPathNameByHandleW(handle.get(), nullptr, 0, flags);
    if (needed == 0)
        return normalizePath(p);

    auto buffer = std::wstring(needed, L'\0');
    auto const written = GetFinalPathNameByHandleW(handle.get(), buffer.data(), needed, flags);
    if (written == 0 || written >= needed)
        return normalizePath(p);
    buffer.resize(written);

    // GetFinalPathNameByHandleW returns an extended-length path. Strip the prefix:
    //   \\?\C:\dir          -> C:\dir
    //   \\?\UNC\server\share -> \\server\share
    auto view = std::wstring_view(buffer);
    auto canonical = std::wstring {};
    if (view.starts_with(LR"(\\?\UNC\)"))
        canonical = L"\\\\" + std::wstring(view.substr(8));
    else if (view.starts_with(LR"(\\?\)"))
        canonical = std::wstring(view.substr(4));
    else
        canonical = std::wstring(view);

    // The DOS volume name already comes back upper-cased, but normalize it defensively.
    if (canonical.size() >= 2 && canonical[1] == L':' && std::iswalpha(static_cast<wint_t>(canonical[0])))
        canonical[0] = static_cast<wchar_t>(std::towupper(static_cast<wint_t>(canonical[0])));

    return normalizePath(std::filesystem::path(canonical));
#else
    return normalizePath(p);
#endif
}

} // namespace endo::platform
