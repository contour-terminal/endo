// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace endo
{

/// Platform-independent job entry returned by JobProvider.
struct JobEntry
{
    int64_t id;          ///< Job number (1-based)
    std::string state;   ///< Job state ("Running", "Stopped", "Done", "Terminated")
    std::string command; ///< Original command string
    int64_t pid;         ///< Process group ID
};

/// Abstract interface for listing shell background jobs.
///
/// Implementations bridge the shell's JobTable to the structured command system.
/// Inject via constructor for testability (mock in tests, real in shell).
class JobProvider
{
  public:
    virtual ~JobProvider() = default;

    /// Enumerates all jobs in the job table.
    /// @return A vector of JobEntry structs, one per job.
    [[nodiscard]] virtual std::vector<JobEntry> listJobs() const = 0;
};

} // namespace endo
