// SPDX-License-Identifier: Apache-2.0
#include "Job.hpp"

#include <algorithm>
#include <ranges>

namespace endo
{

int JobTable::addJob(ProcessId pgid, std::vector<ProcessId> pids, std::string command)
{
    Job job;
    job.id = _nextJobId++;
    job.pgid = pgid;
    job.pids = std::move(pids);
    job.command = std::move(command);
    job.state = JobState::Running;

    _jobs.push_back(std::move(job));

    // Update current/previous job tracking
    _previousJobId = _currentJobId;
    _currentJobId = _jobs.back().id;

    return _jobs.back().id;
}

Job* JobTable::getJob(int jobId)
{
    auto const it = std::ranges::find_if(_jobs, [jobId](Job const& j) { return j.id == jobId; });
    return it != _jobs.end() ? &*it : nullptr;
}

Job const* JobTable::getJob(int jobId) const
{
    auto const it = std::ranges::find_if(_jobs, [jobId](Job const& j) { return j.id == jobId; });
    return it != _jobs.end() ? &*it : nullptr;
}

Job* JobTable::getJobByPgid(ProcessId pgid)
{
    auto const it = std::ranges::find_if(_jobs, [pgid](Job const& j) { return j.pgid == pgid; });
    return it != _jobs.end() ? &*it : nullptr;
}

Job* JobTable::getJobByPid(ProcessId pid)
{
    for (auto& job: _jobs)
    {
        if (std::ranges::find(job.pids, pid) != job.pids.end())
            return &job;
    }
    return nullptr;
}

Job* JobTable::getCurrentJob()
{
    return getJob(_currentJobId);
}

Job* JobTable::getPreviousJob()
{
    return getJob(_previousJobId);
}

void JobTable::updateJobState(ProcessId pid, WaitResult const& result)
{
    Job* job = getJobByPid(pid);
    if (!job)
        return;

    if (result.stopped)
    {
        job->state = JobState::Stopped;
        job->signal = result.signal;
        // Stopped job becomes current
        _previousJobId = _currentJobId;
        _currentJobId = job->id;
    }
    else if (result.signaled)
    {
        job->state = JobState::Terminated;
        job->signal = result.signal;
        job->exitCode = result.exitCode;
    }
    else
    {
        // Check if all processes in the job have exited
        // For simplicity, mark as done when any process exits
        // A more complete implementation would track all PIDs
        job->state = JobState::Done;
        job->exitCode = result.exitCode;
    }
}

void JobTable::cleanupCompletedJobs()
{
    std::erase_if(_jobs, [](Job const& j) {
        return (j.state == JobState::Done || j.state == JobState::Terminated) && j.notified;
    });

    // Fix up current/previous job references if they were removed
    if (!getJob(_currentJobId))
    {
        _currentJobId = _jobs.empty() ? 0 : _jobs.back().id;
    }
    if (!getJob(_previousJobId))
    {
        _previousJobId = 0;
        for (auto const& job: _jobs)
        {
            if (job.id != _currentJobId)
            {
                _previousJobId = job.id;
                break;
            }
        }
    }
}

std::vector<Job const*> JobTable::listJobs() const
{
    std::vector<Job const*> result;
    result.reserve(_jobs.size());
    for (auto const& job: _jobs)
        result.push_back(&job);
    return result;
}

std::vector<Job*> JobTable::getUnnotifiedJobs()
{
    std::vector<Job*> result;
    for (auto& job: _jobs)
    {
        if ((job.state == JobState::Done || job.state == JobState::Terminated) && !job.notified)
            result.push_back(&job);
    }
    return result;
}

void JobTable::removeJob(int jobId)
{
    std::erase_if(_jobs, [jobId](Job const& j) { return j.id == jobId; });

    // Update current/previous if needed
    if (_currentJobId == jobId)
    {
        _currentJobId = _jobs.empty() ? 0 : _jobs.back().id;
    }
    if (_previousJobId == jobId)
    {
        _previousJobId = 0;
        for (auto const& job: _jobs)
        {
            if (job.id != _currentJobId)
            {
                _previousJobId = job.id;
                break;
            }
        }
    }
}

bool JobTable::hasActiveJobs() const
{
    return std::ranges::any_of(
        _jobs, [](Job const& j) { return j.state == JobState::Running || j.state == JobState::Stopped; });
}

void JobTable::setCurrentJob(int jobId)
{
    if (getJob(jobId))
    {
        _previousJobId = _currentJobId;
        _currentJobId = jobId;
    }
}

} // namespace endo
