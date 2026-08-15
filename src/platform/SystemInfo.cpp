// SPDX-License-Identifier: Apache-2.0
#include <array>

#include <platform/SystemInfo.hpp>

#if defined(_WIN32)
    #include <windows.h>
#else
    #include <unistd.h>
#endif

namespace endo::platform
{

std::string hostName()
{
    auto buf = std::array<char, 256> {};
#if defined(_WIN32)
    auto bufLen = static_cast<DWORD>(buf.size());
    if (GetComputerNameA(buf.data(), &bufLen))
        return std::string(buf.data(), bufLen);
#else
    if (gethostname(buf.data(), buf.size()) == 0)
        return std::string(buf.data());
#endif
    return {};
}

std::string const& cachedHostName()
{
    static std::string const cached = hostName();
    return cached;
}

} // namespace endo::platform
