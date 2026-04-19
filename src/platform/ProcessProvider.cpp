// SPDX-License-Identifier: Apache-2.0
#include <platform/ProcessProvider.hpp>

#if defined(_WIN32)
    #include <platform/windows/WindowsProcessProvider.hpp>
#elif defined(__APPLE__)
    #include <platform/darwin/DarwinProcessProvider.hpp>
#else
    #include <platform/linux/LinuxProcessProvider.hpp>
#endif

namespace endo::platform
{

std::unique_ptr<ProcessProvider> createNativeProcessProvider()
{
#if defined(_WIN32)
    return std::make_unique<WindowsProcessProvider>();
#elif defined(__APPLE__)
    return std::make_unique<DarwinProcessProvider>();
#else
    return std::make_unique<LinuxProcessProvider>();
#endif
}

} // namespace endo::platform
