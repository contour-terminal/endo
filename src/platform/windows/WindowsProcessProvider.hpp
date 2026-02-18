// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <platform/ProcessProvider.hpp>

namespace endo::platform
{

/// Windows implementation of ProcessProvider using Toolhelp32 API.
///
/// Uses CreateToolhelp32Snapshot + Process32First/Process32Next for enumeration,
/// GetProcessMemoryInfo for memory usage, GetProcessTimes for CPU usage,
/// and OpenProcessToken + LookupAccountSid for user lookup.
class WindowsProcessProvider final: public ProcessProvider
{
  public:
    [[nodiscard]] std::vector<ProcessEntry> listProcesses() const override;
};

} // namespace endo::platform

// Backward-compatible alias
namespace endo
{
using endo::platform::WindowsProcessProvider;
} // namespace endo
