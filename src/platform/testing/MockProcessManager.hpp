// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <functional>
#include <utility>
#include <vector>

#include <platform/Process.hpp>

namespace endo::platform::testing
{

/// Mock ProcessManager for unit testing.
///
/// Provides configurable responses for spawn, wait, and signal operations.
/// Default behavior: spawn returns PID 1000, wait returns exit code 0.
class MockProcessManager final: public ProcessManager
{
  public:
    /// Callback type for custom spawn behavior.
    using SpawnHandler = std::function<std::expected<ProcessId, PlatformError>(SpawnConfig const&)>;

    /// Callback type for custom wait behavior.
    using WaitHandler = std::function<std::expected<WaitResult, PlatformError>(ProcessId, WaitFlags)>;

    /// Sets a custom spawn handler.
    void onSpawn(SpawnHandler handler) { _spawnHandler = std::move(handler); }

    /// Sets a custom wait handler.
    void onWait(WaitHandler handler) { _waitHandler = std::move(handler); }

    /// Returns the list of spawned configs (for test assertions).
    [[nodiscard]] std::vector<SpawnConfig> const& spawnedConfigs() const noexcept { return _spawnedConfigs; }

    /// Returns the list of signals sent (pid, signal pairs).
    [[nodiscard]] std::vector<std::pair<ProcessId, int>> const& sentSignals() const noexcept
    {
        return _sentSignals;
    }

    [[nodiscard]] std::expected<ProcessId, PlatformError> spawn(SpawnConfig const& config) override
    {
        _spawnedConfigs.push_back(config);
        if (_spawnHandler)
            return _spawnHandler(config);
        return _nextPid++;
    }

    [[nodiscard]] std::expected<WaitResult, PlatformError> wait(ProcessId pid, WaitFlags flags = {}) override
    {
        if (_waitHandler)
            return _waitHandler(pid, flags);
        return WaitResult { .exitCode = 0 };
    }

    [[nodiscard]] std::expected<std::optional<std::pair<ProcessId, WaitResult>>, PlatformError> waitPgid(
        ProcessId /*pgid*/, WaitFlags /*flags*/) override
    {
        return std::nullopt;
    }

    [[nodiscard]] std::expected<void, PlatformError> sendSignal(ProcessId pid, int signal) override
    {
        _sentSignals.emplace_back(pid, signal);
        return {};
    }

    [[nodiscard]] std::expected<ProcessId, PlatformError> getForegroundPgrp(NativeHandle /*fd*/) override
    {
        return static_cast<ProcessId>(1);
    }

    [[nodiscard]] std::expected<void, PlatformError> setForegroundPgrp(NativeHandle /*fd*/,
                                                                       ProcessId /*pgid*/) override
    {
        return {};
    }

    [[nodiscard]] std::expected<NativeHandle, PlatformError> openFile(std::filesystem::path const& /*path*/,
                                                                      int /*flags*/,
                                                                      int /*mode*/) override
    {
        return std::unexpected(PlatformError::NotImplemented);
    }

    [[nodiscard]] std::expected<ProcessId, PlatformError> createSession() override
    {
        return static_cast<ProcessId>(1);
    }

    [[nodiscard]] std::expected<void, PlatformError> setProcessGroup(ProcessId /*pid*/,
                                                                     ProcessId /*pgid*/) override
    {
        return {};
    }

    [[nodiscard]] std::expected<void, PlatformError> duplicateFd(NativeHandle /*src*/,
                                                                 NativeHandle /*dst*/) override
    {
        return {};
    }

    void closeHandle(NativeHandle /*handle*/) noexcept override {}

    void closeExtraHandles() noexcept override {}

  private:
    SpawnHandler _spawnHandler;
    WaitHandler _waitHandler;
    std::vector<SpawnConfig> _spawnedConfigs;
    std::vector<std::pair<ProcessId, int>> _sentSignals;
    ProcessId _nextPid = 1000;
};

} // namespace endo::platform::testing
