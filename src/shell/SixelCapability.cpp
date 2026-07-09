// SPDX-License-Identifier: Apache-2.0
#include <shell/SixelCapability.hpp>
#include <shell/TTY.hpp>

#include <tui/TerminalProtocols.hpp>

#include <chrono>
#include <string>

namespace endo
{

namespace
{
    /// @brief Maximum bytes accepted for a DA1 response, guarding a runaway read.
    constexpr auto MaxResponseBytes = 64uz;

    /// @brief Time to wait for the first byte of the DA1 response.
    constexpr auto FirstByteTimeout = std::chrono::milliseconds(100);

    /// @brief Time to wait for each subsequent byte.
    constexpr auto NextByteTimeout = std::chrono::milliseconds(10);

    /// @brief Restores the terminal's line discipline when the probe returns.
    class RawModeGuard
    {
      public:
        explicit RawModeGuard(TTY& tty): _tty(tty) { _tty.setRawMode(); }

        ~RawModeGuard() { _tty.restoreMode(); }

        RawModeGuard(RawModeGuard const&) = delete;
        auto operator=(RawModeGuard const&) -> RawModeGuard& = delete;
        RawModeGuard(RawModeGuard&&) = delete;
        auto operator=(RawModeGuard&&) -> RawModeGuard& = delete;

      private:
        TTY& _tty;
    };

    /// @brief Sends DA1 and reads the reply, which terminates with 'c'.
    /// @param tty The terminal to query; must already be in raw mode.
    /// @return The raw response, possibly empty on timeout.
    auto queryDeviceAttributes(TTY& tty) -> std::string
    {
        tty.writeToStdout(tui::protocols::QueryPrimaryDeviceAttributes);

        auto response = std::string {};
        auto timeout = FirstByteTimeout;
        while (response.size() < MaxResponseBytes)
        {
            auto const ch = tty.readCharWithTimeout(timeout);
            if (!ch.has_value())
                break;
            response.push_back(*ch);
            if (*ch == 'c')
                break;
            timeout = NextByteTimeout;
        }
        return response;
    }
} // namespace

bool TerminalSixelCapability::supportsSixel()
{
    if (_cached.has_value())
        return *_cached;

    if (auto const override = _env.get("ENDO_SIXEL"))
    {
        _cached = (*override == "1");
        return *_cached;
    }

    if (!_tty.isTerminal())
    {
        _cached = false;
        return false;
    }

    auto const guard = RawModeGuard(_tty);
    _cached = tui::protocols::parseSixelFromDeviceAttributes(queryDeviceAttributes(_tty));
    return *_cached;
}

} // namespace endo
