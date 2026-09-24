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
    using SpawnHandler =
        std::function<std::expected<core::platform::ProcessId, core::platform::PlatformError>(
            SpawnConfig const&)>;

    /// Callback type for custom wait behavior.
    using WaitHandler = std::function<std::expected<WaitResult, core::platform::PlatformError>(
        core::platform::ProcessId, WaitFlags)>;

    /// Sets a custom spawn handler.
    void onSpawn(SpawnHandler handler) { _spawnHandler = std::move(handler); }

    /// Sets a custom wait handler.
    void onWait(WaitHandler handler) { _waitHandler = std::move(handler); }

    /// Returns the list of spawned configs (for test assertions).
    [[nodiscard]] std::vector<SpawnConfig> const& spawnedConfigs() const noexcept { return _spawnedConfigs; }

    /// Returns the list of signals sent (pid, signal pairs).
    [[nodiscard]] std::vector<std::pair<core::platform::ProcessId, int>> const& sentSignals() const noexcept
    {
        return _sentSignals;
    }

    [[nodiscard]] std::expected<core::platform::ProcessId, core::platform::PlatformError> spawn(
        SpawnConfig const& config) override
    {
        _spawnedConfigs.push_back(config);
        if (_spawnHandler)
            return _spawnHandler(config);
        return _nextPid++;
    }

    [[nodiscard]] std::expected<WaitResult, core::platform::PlatformError> wait(core::platform::ProcessId pid,
                                                                                WaitFlags flags = {}) override
    {
        if (_waitHandler)
            return _waitHandler(pid, flags);
        return WaitResult { .exitCode = 0 };
    }

    [[nodiscard]] std::expected<std::optional<std::pair<core::platform::ProcessId, WaitResult>>,
                                core::platform::PlatformError>
    waitPgid(core::platform::ProcessId /*pgid*/, WaitFlags /*flags*/) override
    {
        return std::nullopt;
    }

    [[nodiscard]] std::expected<void, core::platform::PlatformError> sendSignal(core::platform::ProcessId pid,
                                                                                int signal) override
    {
        _sentSignals.emplace_back(pid, signal);
        return {};
    }

    [[nodiscard]] std::expected<core::platform::ProcessId, core::platform::PlatformError> getForegroundPgrp(
        core::platform::NativeHandle /*fd*/) override
    {
        return static_cast<core::platform::ProcessId>(1);
    }

    [[nodiscard]] std::expected<void, core::platform::PlatformError> setForegroundPgrp(
        core::platform::NativeHandle /*fd*/, core::platform::ProcessId /*pgid*/) override
    {
        return {};
    }

    [[nodiscard]] std::expected<core::platform::NativeHandle, core::platform::PlatformError> openFile(
        std::filesystem::path const& /*path*/, int /*flags*/, int /*mode*/) override
    {
        return std::unexpected(core::platform::PlatformError::NotImplemented);
    }

    [[nodiscard]] std::expected<core::platform::ProcessId, core::platform::PlatformError> createSession()
        override
    {
        return static_cast<core::platform::ProcessId>(1);
    }

    [[nodiscard]] std::expected<void, core::platform::PlatformError> setProcessGroup(
        core::platform::ProcessId /*pid*/, core::platform::ProcessId /*pgid*/) override
    {
        return {};
    }

    [[nodiscard]] std::expected<void, core::platform::PlatformError> duplicateFd(
        core::platform::NativeHandle /*src*/, core::platform::NativeHandle /*dst*/) override
    {
        return {};
    }

    void closeHandle(core::platform::NativeHandle /*handle*/) noexcept override {}

    void closeExtraHandles() noexcept override {}

  private:
    SpawnHandler _spawnHandler;
    WaitHandler _waitHandler;
    std::vector<SpawnConfig> _spawnedConfigs;
    std::vector<std::pair<core::platform::ProcessId, int>> _sentSignals;
    core::platform::ProcessId _nextPid = 1000;
};

} // namespace endo::platform::testing
