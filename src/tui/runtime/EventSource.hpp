// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// The blocking multiplexed wait the TUI runtime drives, abstracted behind an
/// interface so the runtime can be unit-tested with a scripted source instead of
/// a real terminal.
///
/// The source is a *registry*: besides the runtime's own fixed sources (terminal
/// input, resize, the agent/interrupt wakeups, the signal fd), arbitrary file
/// descriptors can be attached so a coroutine can `co_await` readiness on a pipe,
/// PTY, or socket. Each `wait()` reports which registered fds became ready via the
/// outcome's @c readyRead / @c readyWrite token lists.

#include <tui/InputEvent.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include <platform/Types.hpp>

namespace tui::runtime
{

/// Readiness interest for a registered file descriptor. A bit set so a single
/// registration can watch readability and writability together.
enum class FdInterest : std::uint8_t
{
    None = 0,         ///< Watch nothing (mute the fd without detaching it).
    Read = 1U << 0U,  ///< Watch for readability (data available / EOF / HUP).
    Write = 1U << 1U, ///< Watch for writability (space available in the send buffer).
};

/// @return The union of two interest masks.
[[nodiscard]] constexpr FdInterest operator|(FdInterest a, FdInterest b) noexcept
{
    return static_cast<FdInterest>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}

/// @param set The mask to test.
/// @param bit The single interest bit to check for.
/// @return True if @p bit is present in @p set.
[[nodiscard]] constexpr bool hasInterest(FdInterest set, FdInterest bit) noexcept
{
    return (static_cast<std::uint8_t>(set) & static_cast<std::uint8_t>(bit)) != 0;
}

/// Opaque token naming one fd registration. Returned by @c EventSource::attach
/// and passed back to @c updateInterest / @c detach. Stable for the
/// registration's lifetime; a value of 0 (the default) signals an invalid /
/// failed registration.
///
/// A strong struct rather than an `enum class` because it is an opaque,
/// monotonically-allocated handle id (a wide value space that never wraps in a
/// session), not an enumeration of named cases.
struct FdToken
{
    std::uint64_t value = 0; ///< Registration id; 0 means invalid.

    /// @return True if two tokens name the same registration.
    [[nodiscard]] friend constexpr bool operator==(FdToken, FdToken) noexcept = default;

    /// @return True if this token names a live registration (non-zero).
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return value != 0; }

    /// Sentinel for an invalid / failed registration.
    [[nodiscard]] static constexpr FdToken invalid() noexcept { return FdToken { 0 }; }
};

/// What a single multiplexed wait observed: any decoded input events, flags for
/// the cross-thread / interrupt sources the runtime selects on, and the tokens of
/// any generic fds that became ready.
struct WaitOutcome
{
    std::vector<InputEvent> events; ///< Input/resize/report events decoded this wait (may be empty).
    bool agentReady = false;        ///< The agent message wakeup fired; drain the agent channel.
    bool interrupted = false;       ///< An interrupt (SIGINT / Ctrl+C) was observed.
    bool activity = false; ///< A non-input wake (focus change, finished job) occurred; resume idle waiters.
    std::vector<FdToken> readyRead;  ///< Registered (non-runtime) fds that became readable this wait.
    std::vector<FdToken> readyWrite; ///< Registered (non-runtime) fds that became writable this wait.
};

/// Abstraction over "block until something happens, with a timeout".
///
/// The real implementation (@c TerminalEventSource) multiplexes terminal input,
/// the resize notification, the agent-message wakeup, the interrupt wakeup, and
/// (on POSIX) the signal fd. Tests inject a scripted source. This is the single
/// dependency-injection seam between the runtime and the OS: keeping it an
/// interface lets `TuiRuntime` be exercised deterministically.
class EventSource
{
  public:
    EventSource() = default;
    virtual ~EventSource() = default;

    EventSource(EventSource const&) = delete;
    EventSource& operator=(EventSource const&) = delete;
    EventSource(EventSource&&) = delete;
    EventSource& operator=(EventSource&&) = delete;

    /// Blocks until input/resize/agent/interrupt activity, a registered fd becomes
    /// ready, or the timeout elapses.
    /// @param timeoutMs -1 to block indefinitely, 0 to poll, >0 to wait that many ms.
    /// @return What was observed during the wait.
    [[nodiscard]] virtual WaitOutcome wait(int timeoutMs) = 0;

    /// Registers @p fd for multiplexing in subsequent waits.
    /// @param fd The native handle to watch (not owned; the caller keeps it valid
    ///        and detaches before closing it).
    /// @param interest The readiness bits to start watching.
    /// @return A token naming the registration, or @c FdToken::invalid() on failure.
    [[nodiscard]] virtual FdToken attach(endo::platform::NativeHandle fd, FdInterest interest) = 0;

    /// Changes the readiness interest of an attached fd. Passing @c FdInterest::None
    /// mutes the fd without detaching it.
    /// @param token The registration to modify.
    /// @param interest The new interest mask.
    virtual void updateInterest(FdToken token, FdInterest interest) = 0;

    /// Removes a registration. Idempotent; a no-op for an unknown or @c Invalid token.
    /// @param token The registration to drop.
    virtual void detach(FdToken token) = 0;
};

} // namespace tui::runtime

namespace std
{

/// Hash specialization so @c FdToken can key an unordered container (the runtime
/// maps tokens to the coroutines parked on them).
template <>
struct hash<tui::runtime::FdToken>
{
    /// @param token The token to hash.
    /// @return The hash of the token's underlying id.
    [[nodiscard]] std::size_t operator()(tui::runtime::FdToken token) const noexcept
    {
        return std::hash<std::uint64_t> {}(token.value);
    }
};

} // namespace std
