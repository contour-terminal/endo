// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file MessageQueue.hpp
/// @brief Thread-safe message queue with optional wakeup integration.

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>
#include <vector>

#include <platform/Wakeup.hpp>

namespace endo::platform
{

/// Thread-safe message queue for inter-thread communication.
///
/// Supports blocking and non-blocking pop, batch draining, and graceful shutdown.
/// Optionally integrates with a Wakeup primitive so that push() can signal
/// a blocked poll()/WaitForMultipleObjects() on the main thread.
///
/// @tparam T The message type. Must be movable.
template <typename T>
class MessageQueue
{
  public:
    MessageQueue() = default;
    ~MessageQueue() = default;

    MessageQueue(MessageQueue const&) = delete;
    MessageQueue& operator=(MessageQueue const&) = delete;
    MessageQueue(MessageQueue&&) = delete;
    MessageQueue& operator=(MessageQueue&&) = delete;

    /// @brief Pushes a message onto the queue.
    /// @param msg The message to enqueue.
    /// @return false if the queue has been shut down, true otherwise.
    auto push(T msg) -> bool
    {
        {
            auto lock = std::unique_lock(_mutex);
            if (_shutdown)
                return false;
            _queue.push(std::move(msg));
        }
        _cv.notify_one();
        if (_wakeup)
            _wakeup->signal();
        return true;
    }

    /// @brief Pops a message, blocking until one is available or shutdown.
    /// @return The message, or std::nullopt if the queue was shut down.
    auto pop() -> std::optional<T>
    {
        auto lock = std::unique_lock(_mutex);
        _cv.wait(lock, [this] { return !_queue.empty() || _shutdown; });
        if (_queue.empty())
            return std::nullopt;
        auto msg = std::move(_queue.front());
        _queue.pop();
        return msg;
    }

    /// @brief Pops a message with a timeout.
    /// @param timeout Maximum time to wait.
    /// @return The message, or std::nullopt on timeout or shutdown.
    auto popFor(std::chrono::milliseconds timeout) -> std::optional<T>
    {
        auto lock = std::unique_lock(_mutex);
        if (!_cv.wait_for(lock, timeout, [this] { return !_queue.empty() || _shutdown; }))
            return std::nullopt; // Timeout.
        if (_queue.empty())
            return std::nullopt; // Shutdown.
        auto msg = std::move(_queue.front());
        _queue.pop();
        return msg;
    }

    /// @brief Tries to pop a message without blocking.
    /// @return The message, or std::nullopt if the queue is empty.
    auto tryPop() -> std::optional<T>
    {
        auto lock = std::unique_lock(_mutex);
        if (_queue.empty())
            return std::nullopt;
        auto msg = std::move(_queue.front());
        _queue.pop();
        return msg;
    }

    /// @brief Drains all available messages into the given vector.
    ///
    /// Non-blocking. Efficiently moves all queued messages in a single lock acquisition.
    /// @param out Vector to append drained messages to.
    /// @return Number of messages drained.
    auto drainTo(std::vector<T>& out) -> size_t
    {
        auto lock = std::unique_lock(_mutex);
        auto const count = _queue.size();
        out.reserve(out.size() + count);
        while (!_queue.empty())
        {
            out.push_back(std::move(_queue.front()));
            _queue.pop();
        }
        return count;
    }

    /// @brief Signals shutdown, unblocking all waiters and rejecting further pushes.
    void shutdown()
    {
        {
            auto lock = std::unique_lock(_mutex);
            _shutdown = true;
        }
        _cv.notify_all();
        if (_wakeup)
            _wakeup->signal();
    }

    /// @brief Resets the queue, clearing the shutdown flag and discarding any remaining messages.
    ///
    /// Call this only when no other threads are accessing the queue (e.g., after joining workers).
    void reset()
    {
        auto lock = std::unique_lock(_mutex);
        _shutdown = false;
        _queue = {};
    }

    /// @brief Returns whether the queue has been shut down.
    [[nodiscard]] auto isShutdown() const -> bool
    {
        auto lock = std::unique_lock(_mutex);
        return _shutdown;
    }

    /// @brief Returns true if the queue is empty.
    [[nodiscard]] auto empty() const -> bool
    {
        auto lock = std::unique_lock(_mutex);
        return _queue.empty();
    }

    /// @brief Returns the number of messages currently in the queue.
    [[nodiscard]] auto size() const -> size_t
    {
        auto lock = std::unique_lock(_mutex);
        return _queue.size();
    }

    /// @brief Sets an optional wakeup primitive.
    ///
    /// When set, push() calls wakeup->signal() after enqueuing,
    /// so a blocked poll()/WaitForMultipleObjects() wakes immediately.
    /// @param wakeup Pointer to the wakeup primitive (must outlive the queue), or nullptr to disable.
    void setWakeup(Wakeup* wakeup) { _wakeup = wakeup; }

  private:
    mutable std::mutex _mutex;
    std::condition_variable _cv;
    std::queue<T> _queue;
    bool _shutdown = false;
    Wakeup* _wakeup = nullptr;
};

} // namespace endo::platform
