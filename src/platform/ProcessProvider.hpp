// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace endo::platform
{

/// Platform-independent process entry returned by ProcessProvider.
struct ProcessEntry
{
    int64_t pid;         ///< Process ID
    int64_t ppid;        ///< Parent process ID
    std::string user;    ///< Owner username
    double cpuPercent;   ///< CPU usage percentage
    int64_t memKb;       ///< Resident memory in kilobytes
    std::string command; ///< Command name or full command line
};

/// Abstract interface for listing system processes.
///
/// Implementations provide platform-specific process enumeration.
/// Inject via constructor for testability (mock in tests, real in shell).
class ProcessProvider
{
  public:
    virtual ~ProcessProvider() = default;

    /// Enumerates all visible processes on the system.
    /// @return A vector of ProcessEntry structs, one per process.
    [[nodiscard]] virtual std::vector<ProcessEntry> listProcesses() const = 0;
};

/// Constructs the platform-native ProcessProvider implementation.
/// Returns a Linux/Darwin/Windows provider depending on the build target.
[[nodiscard]] std::unique_ptr<ProcessProvider> createNativeProcessProvider();

/// How ProcessEntry::command compares against user-supplied program names.
///
/// Windows file names are case-insensitive and executables carry an ".exe"
/// suffix, so both relaxations are enabled there; POSIX platforms compare
/// names verbatim.
struct ProcessNameMatchPolicy
{
    bool caseInsensitive = false; ///< Compare names case-insensitively.
    bool stripExeSuffix = false;  ///< Also match with a trailing ".exe" stripped.
};

/// Returns the process-name match policy of the platform this shell was built for.
[[nodiscard]] ProcessNameMatchPolicy nativeProcessNameMatchPolicy();

} // namespace endo::platform

// Backward-compatible aliases in the endo namespace
namespace endo
{
using endo::platform::createNativeProcessProvider;
using endo::platform::nativeProcessNameMatchPolicy;
using endo::platform::ProcessEntry;
using endo::platform::ProcessNameMatchPolicy;
using endo::platform::ProcessProvider;
} // namespace endo
