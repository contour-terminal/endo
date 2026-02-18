// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <platform/ProcessProvider.hpp>

namespace endo::platform
{

/// Linux implementation of ProcessProvider that reads from /proc.
///
/// Reads /proc/[pid]/stat, /proc/[pid]/status, and /proc/[pid]/cmdline
/// to populate ProcessEntry fields. Gracefully skips unreadable entries.
class LinuxProcessProvider final: public ProcessProvider
{
  public:
    [[nodiscard]] std::vector<ProcessEntry> listProcesses() const override;
};

} // namespace endo::platform

// Backward-compatible alias
namespace endo
{
using endo::platform::LinuxProcessProvider;
} // namespace endo
