// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "GitModule.hpp"

using namespace endo;
using namespace std::chrono_literals;

namespace
{

/// @brief Copyable query stub: one independent promise per **cwd**, created on demand.
///
/// A fetch is identified by its cwd, not by call-arrival order. Each cwd owns its
/// own promise and "finished" flag; the test controls a fetch's result with
/// `fulfill(cwd, GitInfo)` and waits for its worker to complete with `finished(cwd)`.
///
/// Keying by cwd (rather than by an arrival counter) is essential for correctness:
/// when two background fetches are in flight — a superseded one and its replacement —
/// their detached worker threads call this stub in a nondeterministic order. An
/// arrival-counter scheme would then hand whichever worker happened to run first the
/// slot-0 result, so the replacement worker could receive the superseded fetch's
/// result (the original flake: branch-a leaking into /b's cache). Binding by cwd
/// guarantees each worker always receives exactly the result intended for its cwd.
///
/// Shared state lives in a `Shared` struct behind a `shared_ptr`, so every copy of the
/// stub (the test's and the one inside GitModule's `std::function`) sees the same state.
struct ControlledQuery
{
    struct Slot
    {
        std::promise<GitInfo> promise;
        std::atomic<bool> finished { false };
    };

    struct Shared
    {
        std::atomic<int> callCount { 0 };
        std::atomic<std::thread::id> lastThreadId;
        std::mutex mutex;                                   ///< Guards lastCwd and slots.
        std::string lastCwd;                                ///< Most recent cwd a worker queried.
        std::map<std::string, std::shared_ptr<Slot>> slots; ///< Per-cwd promise + finished flag.
    };

    std::shared_ptr<Shared> shared = std::make_shared<Shared>();

    ControlledQuery() = default;

    /// @brief Returns the slot for @p cwd, creating it on first use (under the mutex).
    [[nodiscard]] std::shared_ptr<Slot> slotFor(std::string const& cwd) const
    {
        auto const lock = std::scoped_lock { shared->mutex };
        auto& slot = shared->slots[cwd];
        if (!slot)
            slot = std::make_shared<Slot>();
        return slot;
    }

    GitInfo operator()(std::string const& cwd) const
    {
        shared->callCount.fetch_add(1, std::memory_order_relaxed);
        shared->lastThreadId.store(std::this_thread::get_id(), std::memory_order_relaxed);
        {
            auto const lock = std::scoped_lock { shared->mutex };
            shared->lastCwd = cwd;
        }

        auto const slot = slotFor(cwd);
        auto info = slot->promise.get_future().get();
        // Signal that this fetch's worker has produced its result and is about to
        // return. The module's worker stores `ready` immediately after this returns,
        // so a test that observes `finished(cwd)` can deterministically wait for the
        // fetch to have completed instead of guessing with a fixed sleep.
        slot->finished.store(true, std::memory_order_release);
        return info;
    }

    /// @brief Supplies the result for the fetch of @p cwd, unblocking its worker.
    void fulfill(std::string const& cwd, GitInfo info) const
    {
        slotFor(cwd)->promise.set_value(std::move(info));
    }

    /// @brief Discards @p cwd's slot so the next fetch of the same cwd gets a fresh
    ///        promise. Needed when a test re-fetches the same cwd (e.g. after
    ///        invalidateCache) and must control the second result independently.
    void reset(std::string const& cwd) const
    {
        auto const lock = std::scoped_lock { shared->mutex };
        shared->slots.erase(cwd);
    }

    /// @brief Whether the worker for the fetch of @p cwd has returned from the query.
    [[nodiscard]] bool finished(std::string const& cwd) const
    {
        return slotFor(cwd)->finished.load(std::memory_order_acquire);
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
    auto query = ControlledQuery();
    auto mod = GitModule(query);

    auto const start = std::chrono::steady_clock::now();
    auto const segments = mod.evaluate(ctxAt("/repo"));
    auto const elapsed = std::chrono::steady_clock::now() - start;

    CHECK(elapsed < 100ms);
    CHECK(segments.empty()); // No git info yet — fetch still in flight.

    query.fulfill("/repo", makeGit("main"));
    CHECK(waitUntil([&] { return !mod.evaluate(ctxAt("/repo")).empty(); }));
}

TEST_CASE("GitModule.evaluate_returns_empty_segments_while_fetch_is_pending", "[git][prompt]")
{
    auto query = ControlledQuery();
    auto mod = GitModule(query);

    CHECK(mod.evaluate(ctxAt("/repo")).empty());
    CHECK(mod.evaluate(ctxAt("/repo")).empty());
    // One fetch per distinct cwd — repeated evaluate() while pending is a no-op.
    // The worker may not have run yet; wait until it has observed the call.
    CHECK(waitUntil([&] { return query.calls() == 1; }));

    query.fulfill("/repo", makeGit("main"));
    CHECK(waitUntil([&] { return !mod.evaluate(ctxAt("/repo")).empty(); }));
}

TEST_CASE("GitModule.evaluate_returns_cached_result_after_fetch_completes", "[git][prompt]")
{
    auto query = ControlledQuery();
    auto mod = GitModule(query);

    CHECK(mod.evaluate(ctxAt("/repo")).empty());
    query.fulfill("/repo", makeGit("feature", /*dirty=*/2, /*staged=*/1));

    REQUIRE(waitUntil([&] { return !mod.evaluate(ctxAt("/repo")).empty(); }));

    auto const segments = mod.evaluate(ctxAt("/repo"));
    REQUIRE(segments.size() == 2); // branch + indicator
    CHECK(segments[0].text.find("feature") != std::string::npos);
    CHECK(segments[1].text == " !2 +1");
}

TEST_CASE("GitModule.refreshInterval_is_short_while_pending_and_long_when_idle", "[git][prompt]")
{
    auto query = ControlledQuery();
    auto mod = GitModule(query);

    // Before any evaluate() call, no fetch is in flight.
    REQUIRE(mod.refreshInterval().has_value());
    CHECK(mod.refreshInterval().value() == 5s);

    (void) mod.evaluate(ctxAt("/repo"));

    auto const pending = mod.refreshInterval();
    REQUIRE(pending.has_value());
    CHECK(pending.value() == 150ms);

    query.fulfill("/repo", makeGit("main"));
    // Drain the fetch by calling evaluate() until the cache is populated.
    REQUIRE(waitUntil([&] { return !mod.evaluate(ctxAt("/repo")).empty(); }));

    auto const idle = mod.refreshInterval();
    REQUIRE(idle.has_value());
    CHECK(idle.value() == 5s);
}

TEST_CASE("GitModule.superseded_fetch_is_discarded_when_cwd_changes_mid_flight", "[git][prompt]")
{
    // Two fetches: one for /a (superseded), one for /b (applied).
    auto query = ControlledQuery();
    auto mod = GitModule(query);

    // 1. cd into /a — launches the /a fetch.
    CHECK(mod.evaluate(ctxAt("/a")).empty());

    // 2. cd into /b BEFORE the /a fetch completes — launches the /b fetch and
    //    drops the reference to /a's slot.
    CHECK(mod.evaluate(ctxAt("/b")).empty());
    CHECK(waitUntil([&] { return query.calls() == 2; }));

    // 3. The /a fetch resolves with /a's result. It must NOT populate the cache.
    query.fulfill("/a", makeGit("branch-a"));

    // Deterministically wait for the /a worker to have produced its result, rather
    // than guessing with a fixed sleep (which flaked on slow runners). Once the
    // worker has finished, /a's slot is fully resolved — yet it is already orphaned
    // (the cd to /b reset _pending to /b's slot), so it must never reach the cache.
    REQUIRE(waitUntil([&] { return query.finished("/a"); }));
    CHECK(mod.evaluate(ctxAt("/b")).empty());
    CHECK_FALSE(mod.cachedInfo().valid);

    // 4. The /b fetch resolves with /b's result. This one IS applied.
    query.fulfill("/b", makeGit("branch-b"));
    REQUIRE(waitUntil([&] { return !mod.evaluate(ctxAt("/b")).empty(); }));
    CHECK(mod.cachedInfo().branch == "branch-b");
}

TEST_CASE("GitModule.invalidateCache_triggers_new_background_fetch", "[git][prompt]")
{
    auto query = ControlledQuery();
    auto mod = GitModule(query);

    (void) mod.evaluate(ctxAt("/repo"));
    query.fulfill("/repo", makeGit("main"));
    REQUIRE(waitUntil([&] { return !mod.evaluate(ctxAt("/repo")).empty(); }));
    REQUIRE(query.calls() == 1);

    mod.invalidateCache();

    // The second fetch targets the same cwd; give it a fresh promise so its result
    // is controlled independently of the first.
    query.reset("/repo");

    // First evaluate after invalidate launches a new fetch. The stale cache
    // (from the first fetch) may still be shown while the refresh runs — that is
    // intentional (avoids a brief blank flicker after every command).
    (void) mod.evaluate(ctxAt("/repo"));
    CHECK(waitUntil([&] { return query.calls() == 2; }));

    query.fulfill("/repo", makeGit("main", /*dirty=*/3));
    // Drain the pending fetch on the main thread.
    REQUIRE(waitUntil([&] {
        (void) mod.evaluate(ctxAt("/repo"));
        return mod.cachedInfo().dirty == 3;
    }));
}

TEST_CASE("GitModule.repeated_evaluate_within_ttl_does_not_launch_new_fetch", "[git][prompt]")
{
    auto query = ControlledQuery();
    auto mod = GitModule(query);

    (void) mod.evaluate(ctxAt("/repo"));
    query.fulfill("/repo", makeGit("main"));
    REQUIRE(waitUntil([&] { return !mod.evaluate(ctxAt("/repo")).empty(); }));
    REQUIRE(query.calls() == 1);

    for (auto i = 0; i < 5; ++i)
        (void) mod.evaluate(ctxAt("/repo"));

    // TTL (3 s) has not elapsed — still exactly one invocation.
    CHECK(query.calls() == 1);
}

TEST_CASE("GitModule.query_runs_off_the_main_thread", "[git][prompt]")
{
    auto query = ControlledQuery();
    auto mod = GitModule(query);
    auto const mainId = std::this_thread::get_id();

    (void) mod.evaluate(ctxAt("/repo"));
    // The worker records its thread id before blocking on the promise — a
    // short poll is enough to observe it.
    CHECK(waitUntil([&] { return query.workerThreadId() != std::thread::id {}; }));
    CHECK(query.workerThreadId() != mainId);

    query.fulfill("/repo", makeGit("main"));
    // Drain on the main thread — consumePendingIfReady runs inside evaluate().
    CHECK(waitUntil([&] {
        (void) mod.evaluate(ctxAt("/repo"));
        return mod.cachedInfo().valid;
    }));
}
