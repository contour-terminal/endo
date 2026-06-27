// SPDX-License-Identifier: Apache-2.0
#if defined(__APPLE__)

    #include <catch2/catch_test_macros.hpp>

    #include <algorithm>
    #include <ranges>

    #include <unistd.h>

    #include "darwin/DarwinProcessProvider.hpp"

using namespace endo::platform;

TEST_CASE("DarwinProcessProvider.lists_current_process", "[platform][darwin]")
{
    DarwinProcessProvider provider;
    auto const processes = provider.listProcesses();

    auto const myPid = static_cast<int64_t>(getpid());
    auto const it = std::ranges::find(processes, myPid, &ProcessEntry::pid);
    REQUIRE(it != processes.end());
    CHECK(it->ppid > 0);
    CHECK(!it->user.empty());
    CHECK(!it->command.empty());
    CHECK(it->memKb > 0);
}

TEST_CASE("DarwinProcessProvider.lists_init_process", "[platform][darwin]")
{
    DarwinProcessProvider provider;
    auto const processes = provider.listProcesses();

    // PID 1 (launchd) should always exist on macOS
    auto const it = std::ranges::find(processes, int64_t { 1 }, &ProcessEntry::pid);
    REQUIRE(it != processes.end());
    CHECK(it->ppid == 0);
    CHECK(it->command == "launchd");
}

TEST_CASE("DarwinProcessProvider.sorted_by_pid", "[platform][darwin]")
{
    DarwinProcessProvider provider;
    auto const processes = provider.listProcesses();
    REQUIRE(!processes.empty());
    CHECK(std::ranges::is_sorted(processes, {}, &ProcessEntry::pid));
}

TEST_CASE("DarwinProcessProvider.fields_are_reasonable", "[platform][darwin]")
{
    DarwinProcessProvider provider;
    auto const processes = provider.listProcesses();
    REQUIRE(!processes.empty());
    for (auto const& p: processes)
    {
        CHECK(p.pid > 0);
        CHECK(p.cpuPercent >= 0.0);
        CHECK(p.memKb >= 0);
    }
}

#endif // __APPLE__
