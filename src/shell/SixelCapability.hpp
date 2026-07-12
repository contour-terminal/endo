// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>

#include <platform/EnvironmentProvider.hpp>

namespace endo
{

class TTY;

/// @brief Answers whether the terminal can display Sixel images.
///
/// Abstracted so that rendering code never performs terminal I/O itself, and so
/// tests can force either answer without a terminal.
class SixelCapabilityProvider
{
  public:
    virtual ~SixelCapabilityProvider() = default;

    SixelCapabilityProvider() = default;
    SixelCapabilityProvider(SixelCapabilityProvider const&) = delete;
    auto operator=(SixelCapabilityProvider const&) -> SixelCapabilityProvider& = delete;
    SixelCapabilityProvider(SixelCapabilityProvider&&) = delete;
    auto operator=(SixelCapabilityProvider&&) -> SixelCapabilityProvider& = delete;

    /// @brief Whether Sixel graphics may be emitted.
    /// @return true when the terminal advertises Sixel support.
    [[nodiscard]] virtual bool supportsSixel() = 0;
};

/// @brief Reports a fixed capability. Intended for tests.
class StaticSixelCapability final: public SixelCapabilityProvider
{
  public:
    /// @param supported The value supportsSixel() should return.
    explicit StaticSixelCapability(bool supported) noexcept: _supported(supported) {}

    [[nodiscard]] bool supportsSixel() override { return _supported; }

  private:
    bool _supported;
};

/// @brief Detects Sixel support from the environment, then from the terminal.
///
/// Resolution order, each answer cached for the process lifetime:
/// 1. `ENDO_SIXEL=0` / `ENDO_SIXEL=1` forces the answer and skips the probe.
/// 2. A Primary Device Attributes (DA1) query, performed only when the terminal
///    is interactive. The terminal must be in raw mode for the response not to
///    be echoed, so the query is delegated to the injected TTY.
/// 3. Otherwise false.
class TerminalSixelCapability final: public SixelCapabilityProvider
{
  public:
    /// @param tty The terminal to probe.
    /// @param env The environment, consulted for the `ENDO_SIXEL` override.
    TerminalSixelCapability(TTY& tty, EnvironmentProvider& env) noexcept: _tty(tty), _env(env) {}

    [[nodiscard]] bool supportsSixel() override;

  private:
    TTY& _tty;
    EnvironmentProvider& _env;
    std::optional<bool> _cached; ///< Probe result, computed at most once.
};

} // namespace endo
