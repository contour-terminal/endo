// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <thread>
#include <vector>

#include <platform/MessageQueue.hpp>

#ifndef _WIN32
    #include <poll.h>
#endif

using namespace endo::platform;

TEST_CASE("MessageQueue.push_and_pop", "[platform][messagequeue]")
{
    auto queue = MessageQueue<int> {};
    REQUIRE(queue.push(42));
    auto const msg = queue.pop();
    REQUIRE(msg.has_value());
    CHECK(*msg == 42);
}

TEST_CASE("MessageQueue.tryPop_empty", "[platform][messagequeue]")
{
    auto queue = MessageQueue<int> {};
    auto const msg = queue.tryPop();
    CHECK_FALSE(msg.has_value());
}

TEST_CASE("MessageQueue.tryPop_nonempty", "[platform][messagequeue]")
{
    auto queue = MessageQueue<int> {};
    queue.push(7);
    auto const msg = queue.tryPop();
    REQUIRE(msg.has_value());
    CHECK(*msg == 7);
    CHECK_FALSE(queue.tryPop().has_value());
}

TEST_CASE("MessageQueue.popFor_timeout", "[platform][messagequeue]")
{
    auto queue = MessageQueue<int> {};
    auto const msg = queue.popFor(std::chrono::milliseconds(10));
    CHECK_FALSE(msg.has_value());
}

TEST_CASE("MessageQueue.popFor_available", "[platform][messagequeue]")
{
    auto queue = MessageQueue<int> {};
    queue.push(99);
    auto const msg = queue.popFor(std::chrono::milliseconds(100));
    REQUIRE(msg.has_value());
    CHECK(*msg == 99);
}

TEST_CASE("MessageQueue.drainTo", "[platform][messagequeue]")
{
    auto queue = MessageQueue<int> {};
    queue.push(1);
    queue.push(2);
    queue.push(3);

    auto batch = std::vector<int> {};
    auto const count = queue.drainTo(batch);
    CHECK(count == 3);
    REQUIRE(batch.size() == 3);
    CHECK(batch[0] == 1);
    CHECK(batch[1] == 2);
    CHECK(batch[2] == 3);
    CHECK(queue.empty());
}

TEST_CASE("MessageQueue.shutdown_unblocks_pop", "[platform][messagequeue]")
{
    auto queue = MessageQueue<int> {};
    auto result = std::optional<int> { 42 }; // sentinel

    auto worker = std::jthread([&](const std::stop_token&) { result = queue.pop(); });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    queue.shutdown();
    worker.join();

    CHECK_FALSE(result.has_value());
}

TEST_CASE("MessageQueue.push_after_shutdown_returns_false", "[platform][messagequeue]")
{
    auto queue = MessageQueue<int> {};
    queue.shutdown();
    CHECK_FALSE(queue.push(42));
    CHECK(queue.isShutdown());
}

TEST_CASE("MessageQueue.cross_thread_push_pop", "[platform][messagequeue]")
{
    auto queue = MessageQueue<std::string> {};

    auto producer = std::jthread([&](const std::stop_token&) {
        for (auto i = 0; i < 10; ++i)
            queue.push("msg-" + std::to_string(i));
    });

    producer.join();

    auto batch = std::vector<std::string> {};
    queue.drainTo(batch);
    CHECK(batch.size() == 10);
}

TEST_CASE("MessageQueue.wakeup_integration", "[platform][messagequeue]")
{
    auto wakeup = Wakeup {};
    auto queue = MessageQueue<int> {};
    queue.setWakeup(&wakeup);

    // Push should signal the wakeup.
    queue.push(42);

    // Verify wakeup was signaled (platform-specific check).
    // On POSIX, the native handle should be readable.
#ifndef _WIN32
    auto pfd = pollfd { .fd = wakeup.nativeHandle(), .events = POLLIN, .revents = 0 };
    auto const result = ::poll(&pfd, 1, 0);
    CHECK(result == 1);
#endif

    wakeup.reset();
    auto const msg = queue.tryPop();
    REQUIRE(msg.has_value());
    CHECK(*msg == 42);
}

TEST_CASE("MessageQueue.size_and_empty", "[platform][messagequeue]")
{
    auto queue = MessageQueue<int> {};
    CHECK(queue.empty());
    CHECK(queue.empty());

    queue.push(1);
    queue.push(2);
    CHECK_FALSE(queue.empty());
    CHECK(queue.size() == 2);

    queue.tryPop();
    CHECK(queue.size() == 1);
}
