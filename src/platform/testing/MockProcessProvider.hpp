// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <utility>

#include <platform/ProcessProvider.hpp>

namespace endo::platform::testing
{

/// Mock ProcessProvider for unit testing.
///
/// Returns a configurable list of ProcessEntry instances.
class MockProcessProvider final: public ProcessProvider
{
  public:
    /// Sets the list of processes to return.
    void setProcesses(std::vector<ProcessEntry> entries) { _entries = std::move(entries); }

    [[nodiscard]] std::vector<ProcessEntry> listProcesses() const override { return _entries; }

  private:
    std::vector<ProcessEntry> _entries;
};

} // namespace endo::platform::testing
