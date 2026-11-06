// SPDX-License-Identifier: Apache-2.0
#include <platform/windows/HandleInheritance.hpp>

namespace endo::platform
{

std::mutex& handleInheritanceMutex() noexcept
{
    static auto mutex = std::mutex {};
    return mutex;
}

} // namespace endo::platform
