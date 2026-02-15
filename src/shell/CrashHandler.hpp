// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace endo
{

/// @brief Crash handler that catches fatal signals and writes backtrace logs.
///
/// Installs signal handlers for SIGSEGV, SIGABRT, SIGBUS, SIGILL, and SIGFPE.
/// On crash, writes a backtrace log to ~/.local/state/endo/crash/ and a brief
/// message to stderr, then re-raises the signal for the default handler (core dump).
///
/// Must be initialized early in main(), before Shell construction.
/// Uses only async-signal-safe functions in the signal handler.
///
/// Supported on Linux and macOS (both provide execinfo.h).
/// Windows is a no-op.
class CrashHandler
{
  public:
    /// @brief Installs crash signal handlers.
    ///
    /// Resolves the crash log directory path from $HOME, creates it if needed,
    /// and installs sigaction handlers for fatal signals.
    /// Must be called early in main(), before any Shell construction.
    ///
    /// @param version The endo version string to include in crash logs.
    static void initialize(char const* version);
};

} // namespace endo
