// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "GitModule.hpp"

using namespace endo;
using namespace std::chrono_literals;

namespace
{

/// @brief Copyable query stub: N independent promises, one per expected fetch.
///
/// Each invocation consumes the next promise. The test fulfills them in order
/// via `fulfill(index, GitInfo)`. Shared state is held in a `Shared` struct
/// via `shared_ptr`, so every copy of the stub (the test's copy and the one
/// stored inside GitModule's `std::function`) sees the same state.
struct ControlledQuery
{
    struct Shared
    {
        std::atomic<int> callCount { 0 };
        std::vector<std::shared_ptr<std::promise<GitInfo>>> promises;
        std::atomic<std::thread::id> lastThreadId;
        std::mutex cwdMutex;
        std::string lastCwd;
    };

    std::shared_ptr<Shared> shared = std::make_shared<Shared>();

    explicit ControlledQuery(int expectedCalls = 1)
    {
        for (auto i = 0; i < expectedCalls; ++i)
            shared->promises.push_back(std::make_shared<std::promise<GitInfo>>());
    }

    GitInfo operator()(std::string const& cwd) const
    {
        auto const n = shared->callCount.fetch_add(1, std::memory_order_relaxed);
        shared->lastThreadId.store(std::this_thread::get_id(), std::memory_order_relaxed);
        {
            auto const lock = std::scoped_lock { shared->cwdMutex };
            shared->lastCwd = cwd;
        }
        // at() throws if the test didn't reserve enough promises — turns a
        // missing expectation into a clear test failure rather than UB.
        return shared->promises.at(static_cast<size_t>(n))->get_future().get();
    }

    void fulfill(int index, GitInfo info) const
    {
        shared->promises.at(static_cast<size_t>(index))->set_value(std::move(info));
    }

    [[nodiscard]] int calls() const { return shared->callCount.load(std::memory_order_relaxed); }

    [[nodiscard]] std::thread::id workerThreadId() const
    {
        return shared->lastThreadId.load(std::memory_order_relaxed);
    }
};

GitInfo makeGit(std::string branch, int dirty = 0, int staged = 0)
{
    return GitInfo { .branch = std::move(branch), .dirty = dirty, .staged = staged, .valid = true };
}

PromptContext ctxAt(std::string cwd)
{
    auto ctx = PromptContext {};
    ctx.cwd = std::move(cwd);
    return ctx;
}

/// @brief Polls a predicate every 1 ms up to a deadline — avoids fixed sleeps.
template <typename Pred>
bool waitUntil(Pred pred, std::chrono::milliseconds timeout = 2s)
{
    auto const deadline = std::chrono::steady_clock::now() + timeout;
    while (!pred())
    {
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

} // namespace

TEST_CASE("GitModule.issue_99_evaluate_does_not_block_on_slow_query", "[git][prompt]")
{
    // Reproducer for issue #99: a slow `git status` must not stall the prompt.
    // Before the fix, evaluate() blocked on popen; now it must return promptly
    // and surface the data on a later call once the worker has finished.
    auto query = ControlledQuery { /*expectedCalls=*/1 };
    auto mod = GitModule(query);

    auto const start = std::chrono::steady_clock::now();
    auto const segments = mod.evaluate(ctxAt("/repo"));
    auto const elapsed = std::chrono::steady_clock::now() - start;

    CHECK(elapsed < 100ms);
    CHECK(segments.empty()); // No git info yet — fetch still in flight.

    query.fulfill(0, makeGit("main"));
    CHECK(waitUntil([&] { return !mod.evaluate(ctxAt("/repo")).empty(); }));
}

TEST_CASE("GitModule.evaluate_returns_empty_segments_while_fetch_is_pending", "[git][prompt]")
{
    auto query = ControlledQuery { 1 };
    auto mod = GitModule(query);

    CHECK(mod.evaluate(ctxAt("/repo")).empty());
    CHECK(mod.evaluate(ctxAt("/repo")).empty());
    // One fetch per distinct cwd — repeated evaluate() while pending is a no-op.
    // The worker may not have run yet; wait until it has observed the call.
    CHECK(waitUntil([&] { return query.calls() == 1; }));

    query.fulfill(0, makeGit("main"));
    CHECK(waitUntil([&] { return !mod.evaluate(ctxAt("/repo")).empty(); }));
}

TEST_CASE("GitModule.evaluate_returns_cached_result_after_fetch_completes", "[git][prompt]")
{
    auto query = ControlledQuery { 1 };
    auto mod = GitModule(query);

    CHECK(mod.evaluate(ctxAt("/repo")).empty());
    query.fulfill(0, makeGit("feature", /*dirty=*/2, /*staged=*/1));

    REQUIRE(waitUntil([&] { return !mod.evaluate(ctxAt("/repo")).empty(); }));

    auto const segments = mod.evaluate(ctxAt("/repo"));
    REQUIRE(segments.size() == 2); // branch + indicator
    CHECK(segments[0].text.find("feature") != std::string::npos);
    CHECK(segments[1].text == " !2 +1");
}

TEST_CASE("GitModule.refreshInterval_is_short_while_pending_and_long_when_idle", "[git][prompt]")
{
    auto query = ControlledQuery { 1 };
    auto mod = GitModule(query);

    // Before any evaluate() call, no fetch is in flight.
    REQUIRE(mod.refreshInterval().has_value());
    CHECK(mod.refreshInterval().value() == 5s);

    (void) mod.evaluate(ctxAt("/repo"));

    auto const pending = mod.refreshInterval();
    REQUIRE(pending.has_value());
    CHECK(pending.value() == 150ms);

    query.fulfill(0, makeGit("main"));
    // Drain the fetch by calling evaluate() until the cache is populated.
    REQUIRE(waitUntil([&] { return !mod.evaluate(ctxAt("/repo")).empty(); }));

    auto const idle = mod.refreshInterval();
    REQUIRE(idle.has_value());
    CHECK(idle.value() == 5s);
}

TEST_CASE("GitModule.superseded_fetch_is_discarded_when_cwd_changes_mid_flight", "[git][prompt]")
{
    // Two fetches: one for /a (superseded), one for /b (applied).
    auto query = ControlledQuery { 2 };
    auto mod = GitModule(query);

    // 1. cd into /a — launches fetch #0 for /a.
    CHECK(mod.evaluate(ctxAt("/a")).empty());

    // 2. cd into /b BEFORE fetch #0 completes — launches fetch #1 for /b and
    //    drops the reference to fetch #0's slot.
    CHECK(mod.evaluate(ctxAt("/b")).empty());
    CHECK(waitUntil([&] { return query.calls() == 2; }));

    // 3. Fetch #0 resolves with /a's result. It must NOT populate the cache.
    query.fulfill(0, makeGit("branch-a"));

    // Let the worker finish its store; a short poll window is enough because
    // we're only racing on the worker storing `ready = true`.
    std::this_thread::sleep_for(50ms);
    CHECK(mod.evaluate(ctxAt("/b")).empty());
    CHECK_FALSE(mod.cachedInfo().valid);

    // 4. Fetch #1 resolves with /b's result. This one IS applied.
    query.fulfill(1, makeGit("branch-b"));
    REQUIRE(waitUntil([&] { return !mod.evaluate(ctxAt("/b")).empty(); }));
    CHECK(mod.cachedInfo().branch == "branch-b");
}

TEST_CASE("GitModule.invalidateCache_triggers_new_background_fetch", "[git][prompt]")
{
    auto query = ControlledQuery { 2 };
    auto mod = GitModule(query);

    (void) mod.evaluate(ctxAt("/repo"));
    query.fulfill(0, makeGit("main"));
    REQUIRE(waitUntil([&] { return !mod.evaluate(ctxAt("/repo")).empty(); }));
    REQUIRE(query.calls() == 1);

    mod.invalidateCache();

    // First evaluate after invalidate launches a new fetch. The stale cache
    // (from fetch #0) may still be shown while the refresh runs — that is
    // intentional (avoids a brief blank flicker after every command).
    (void) mod.evaluate(ctxAt("/repo"));
    CHECK(waitUntil([&] { return query.calls() == 2; }));

    query.fulfill(1, makeGit("main", /*dirty=*/3));
    // Drain the pending fetch on the main thread.
    REQUIRE(waitUntil([&] {
        (void) mod.evaluate(ctxAt("/repo"));
        return mod.cachedInfo().dirty == 3;
    }));
}

TEST_CASE("GitModule.repeated_evaluate_within_ttl_does_not_launch_new_fetch", "[git][prompt]")
{
    auto query = ControlledQuery { 1 };
    auto mod = GitModule(query);

    (void) mod.evaluate(ctxAt("/repo"));
    query.fulfill(0, makeGit("main"));
    REQUIRE(waitUntil([&] { return !mod.evaluate(ctxAt("/repo")).empty(); }));
    REQUIRE(query.calls() == 1);

    for (auto i = 0; i < 5; ++i)
        (void) mod.evaluate(ctxAt("/repo"));

    // TTL (3 s) has not elapsed — still exactly one invocation.
    CHECK(query.calls() == 1);
}

TEST_CASE("GitModule.query_runs_off_the_main_thread", "[git][prompt]")
{
    auto query = ControlledQuery { 1 };
    auto mod = GitModule(query);
    auto const mainId = std::this_thread::get_id();

    (void) mod.evaluate(ctxAt("/repo"));
    // The worker records its thread id before blocking on the promise — a
    // short poll is enough to observe it.
    CHECK(waitUntil([&] { return query.workerThreadId() != std::thread::id {}; }));
    CHECK(query.workerThreadId() != mainId);

    query.fulfill(0, makeGit("main"));
    // Drain on the main thread — consumePendingIfReady runs inside evaluate().
    CHECK(waitUntil([&] {
        (void) mod.evaluate(ctxAt("/repo"));
        return mod.cachedInfo().valid;
    }));
}
