// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Error.hpp"
#include <platform/Types.hpp>

namespace endo
{

/// State of a background job
enum class JobState // NOLINT(performance-enum-size)
{
    Running,    ///< Job is currently running
    Stopped,    ///< Job was stopped (e.g., by SIGTSTP)
    Done,       ///< Job completed normally
    Terminated, ///< Job was terminated by a signal
};

/// Represents a background job in the shell
struct Job
{
    int id = 0;                        ///< Job number (1-based, displayed as [N])
    ProcessId pgid = InvalidProcessId; ///< Process group ID for the job
    std::vector<ProcessId> pids;       ///< All PIDs in this job (for pipelines)
    std::string command;               ///< Original command string
    JobState state = JobState::Running;
    int exitCode = 0;      ///< Exit code (valid when state is Done)
    int signal = 0;        ///< Signal number (valid when terminated/stopped)
    bool notified = false; ///< Whether user has been notified of completion
};

/// Manages the table of background jobs
class JobTable
{
  public:
    /// Adds a new job to the table.
    ///
    /// @param pgid Process group ID for the job
    /// @param pids All process IDs in the job
    /// @param command The command string
    /// @return The assigned job ID
    [[nodiscard]] int addJob(ProcessId pgid, std::vector<ProcessId> pids, std::string command);

    /// Gets a job by its job ID.
    ///
    /// @param jobId The job ID (1-based)
    /// @return Pointer to the job, or nullptr if not found
    [[nodiscard]] Job* getJob(int jobId);

    /// Gets a job by its job ID (const version).
    [[nodiscard]] Job const* getJob(int jobId) const;

    /// Gets a job by its process group ID.
    ///
    /// @param pgid Process group ID
    /// @return Pointer to the job, or nullptr if not found
    [[nodiscard]] Job* getJobByPgid(ProcessId pgid);

    /// Gets a job containing a specific PID.
    ///
    /// @param pid Process ID to search for
    /// @return Pointer to the job, or nullptr if not found
    [[nodiscard]] Job* getJobByPid(ProcessId pid);

    /// Gets the current job (most recently backgrounded or stopped).
    [[nodiscard]] Job* getCurrentJob();

    /// Gets the previous job.
    [[nodiscard]] Job* getPreviousJob();

    /// Updates the state of a job based on wait result.
    ///
    /// @param pid Process ID that changed state
    /// @param result The wait result
    void updateJobState(ProcessId pid, WaitResult const& result);

    /// Removes completed jobs that have been notified.
    void cleanupCompletedJobs();

    /// Lists all jobs.
    [[nodiscard]] std::vector<Job const*> listJobs() const;

    /// Gets jobs that have completed but haven't been reported to user.
    [[nodiscard]] std::vector<Job*> getUnnotifiedJobs();

    /// Removes a job from the table.
    ///
    /// @param jobId The job ID to remove
    void removeJob(int jobId);

    /// Checks if there are any active (running or stopped) jobs.
    [[nodiscard]] bool hasActiveJobs() const;

    /// Sets the current job.
    void setCurrentJob(int jobId);

  private:
    std::vector<Job> _jobs;
    int _nextJobId = 1;
    int _currentJobId = 0;  ///< Current job (marked with +)
    int _previousJobId = 0; ///< Previous job (marked with -)
};

/// Converts JobState to a string for display
[[nodiscard]] constexpr std::string_view toString(JobState state) noexcept
{
    switch (state)
    {
        case JobState::Running: return "Running";
        case JobState::Stopped: return "Stopped";
        case JobState::Done: return "Done";
        case JobState::Terminated: return "Terminated";
    }
    return "Unknown";
}

} // namespace endo
