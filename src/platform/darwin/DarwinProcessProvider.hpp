// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <platform/ProcessProvider.hpp>

namespace endo::platform
{

/// macOS implementation of ProcessProvider using sysctl and libproc APIs.
class DarwinProcessProvider final: public ProcessProvider
{
  public:
    [[nodiscard]] std::vector<ProcessEntry> listProcesses() const override;
};

} // namespace endo::platform

// Backward-compatible alias
namespace endo
{
using endo::platform::DarwinProcessProvider;
} // namespace endo
