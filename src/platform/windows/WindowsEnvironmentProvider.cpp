// SPDX-License-Identifier: Apache-2.0
#include "WindowsEnvironmentProvider.hpp"

#if defined(_WIN32)

    #include <algorithm>
    #include <cctype>
    #include <filesystem>

    #include <windows.h>

    #include <platform/PathUtils.hpp>

namespace endo::platform
{

bool WindowsEnvironmentProvider::CaseInsensitiveLess::operator()(std::string const& a,
                                                                 std::string const& b) const
{
    return std::lexicographical_compare(
        a.begin(), a.end(), b.begin(), b.end(), [](unsigned char ac, unsigned char bc) {
            return std::tolower(ac) < std::tolower(bc);
        });
}

WindowsEnvironmentProvider& WindowsEnvironmentProvider::instance()
{
    static WindowsEnvironmentProvider provider;
    return provider;
}

void WindowsEnvironmentProvider::set(std::string_view name, std::string_view value)
{
    _values[std::string(name)] = std::string(value);
}

std::optional<std::string> WindowsEnvironmentProvider::get(std::string_view name) const
{
    if (auto const it = _values.find(std::string(name)); it != _values.end())
        return it->second;

    std::string buffer(32767, '\0');

    auto const nameStr = std::string(name);
    auto const len =
        GetEnvironmentVariableA(nameStr.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()));
    if (len == 0)
        return std::nullopt;

    buffer.resize(len);
    return buffer;
}

void WindowsEnvironmentProvider::unset(std::string_view name)
{
    _values.erase(std::string(name));
    auto const nameStr = std::string(name);
    SetEnvironmentVariableA(nameStr.c_str(), nullptr);
}

void WindowsEnvironmentProvider::exportVariable(std::string_view name)
{
    auto const it = _values.find(std::string(name));
    if (it != _values.end())
        SetEnvironmentVariableA(it->first.c_str(), it->second.c_str());
}

std::vector<std::string> WindowsEnvironmentProvider::keys() const
{
    std::vector<std::string> result;

    auto const envBlock = GetEnvironmentStringsW();
    if (envBlock != nullptr)
    {
        for (auto const* p = envBlock; *p != L'\0';)
        {
            auto const entry = std::wstring_view(p);
            auto const eq = entry.find(L'=');
            if (eq != std::wstring_view::npos && eq > 0)
            {
                auto const wideKey = entry.substr(0, eq);
                std::string narrowKey;
                narrowKey.reserve(wideKey.size());
                for (auto const wc: wideKey)
                    narrowKey += static_cast<char>(wc);
                result.push_back(std::move(narrowKey));
            }
            p += entry.size() + 1;
        }
        FreeEnvironmentStringsW(envBlock);
    }

    for (auto const& [key, _]: _values)
    {
        auto const found = std::find_if(result.begin(), result.end(), [&](std::string const& existing) {
            return std::equal(existing.begin(), existing.end(), key.begin(), key.end(), [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a))
                       == std::tolower(static_cast<unsigned char>(b));
            });
        });
        if (found == result.end())
            result.push_back(key);
    }

    return result;
}

std::expected<void, PlatformError> WindowsEnvironmentProvider::changeDirectory(
    std::filesystem::path const& path)
{
    std::error_code ec;
    std::filesystem::current_path(path, ec);
    if (ec)
        return std::unexpected(PlatformError::IoError);
    return {};
}

std::string WindowsEnvironmentProvider::currentDirectory() const
{
    // Report the real on-disk capitalization (and an upper-case drive letter) so that
    // PWD and the prompt agree with how the directory is actually stored, rather than
    // echoing whatever case was passed to SetCurrentDirectory.
    return canonicalCasePath(std::filesystem::current_path());
}

} // namespace endo::platform

#endif // _WIN32
