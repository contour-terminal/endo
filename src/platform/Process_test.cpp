// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <csignal>

#include <platform/testing/MockFileInfoProvider.hpp>
#include <platform/testing/MockProcessManager.hpp>
#include <platform/testing/MockProcessProvider.hpp>

using namespace endo::platform;

TEST_CASE("MockProcessManager.spawn_default", "[platform][mock]")
{
    testing::MockProcessManager pm;
    SpawnConfig config;
    config.program = "/bin/echo";
    config.arguments = { "hello" };

    auto result = pm.spawn(config);
    REQUIRE(result.has_value());
    CHECK(*result == 1000);

    CHECK(pm.spawnedConfigs().size() == 1);
    CHECK(pm.spawnedConfigs()[0].program == "/bin/echo");
}

TEST_CASE("MockProcessManager.spawn_custom_handler", "[platform][mock]")
{
    testing::MockProcessManager pm;
    pm.onSpawn([](SpawnConfig const&) -> std::expected<ProcessId, PlatformError> {
        return std::unexpected(PlatformError::ProgramNotFound);
    });

    SpawnConfig config;
    config.program = "/nonexistent";

    auto result = pm.spawn(config);
    CHECK(!result.has_value());
    CHECK(result.error() == PlatformError::ProgramNotFound);
}

TEST_CASE("MockProcessManager.wait_default", "[platform][mock]")
{
    testing::MockProcessManager pm;
    auto result = pm.wait(1000);
    REQUIRE(result.has_value());
    CHECK(result->exitCode == 0);
}

TEST_CASE("MockProcessManager.sendSignal", "[platform][mock]")
{
    testing::MockProcessManager pm;
    auto result = pm.sendSignal(1000, SIGTERM);
    CHECK(result.has_value());
    CHECK(pm.sentSignals().size() == 1);
    CHECK(pm.sentSignals()[0].first == 1000);
    CHECK(pm.sentSignals()[0].second == SIGTERM);
}

TEST_CASE("MockProcessProvider.basic", "[platform][mock]")
{
    testing::MockProcessProvider pp;
    pp.setProcesses(
        { { .pid = 1, .ppid = 0, .user = "root", .cpuPercent = 0.0, .memKb = 1024, .command = "init" } });

    auto const procs = pp.listProcesses();
    REQUIRE(procs.size() == 1);
    CHECK(procs[0].pid == 1);
    CHECK(procs[0].command == "init");
}

TEST_CASE("MockFileInfoProvider.basic", "[platform][mock]")
{
    testing::MockFileInfoProvider fip;
    fip.setEntries("/tmp", { { .name = "foo.txt", .size = 42, .mode = 0644, .mtime = 0, .isDir = false } });

    auto const entries = fip.listDirectory("/tmp");
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].name == "foo.txt");
    CHECK(entries[0].size == 42);

    auto const empty = fip.listDirectory("/nonexistent");
    CHECK(empty.empty());
}
