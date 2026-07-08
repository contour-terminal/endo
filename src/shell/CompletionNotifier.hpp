// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// Dependency-injected sink for completion-wait notifications.
///
/// When a tab-completion's command substitution is aborted (Escape/Ctrl+C) or
/// times out, the shell surfaces a short diagnostic. Routing that through an
/// injected interface keeps the policy swappable: the default writes a one-line
/// terminal notice, but it can be replaced with a no-op (tests, non-interactive
/// use), a GUI popup, or a system notification without touching the abort logic.

#include <cstdint>
#include <string_view>

namespace endo
{

/// Why a completion wait ended early.
enum class NotificationKind : std::uint8_t
{
    Aborted,  ///< The user cancelled the completion (Escape / Ctrl+C).
    TimedOut, ///< The completion command exceeded its time budget.
};

/// Surfaces completion-wait outcomes to the user. Injected into @c Shell so the
/// presentation (terminal tooltip / GUI / system notification / off) can vary.
class CompletionNotifier
{
  public:
    virtual ~CompletionNotifier() = default;

    /// Presents a completion-wait notification.
    /// @param kind What happened (aborted or timed out).
    /// @param message A short, already-formatted human-readable message.
    virtual void notify(NotificationKind kind, std::string_view message) = 0;
};

/// A @c CompletionNotifier that discards every notification. Used in tests and any
/// context where completion diagnostics should be suppressed.
class NullCompletionNotifier final: public CompletionNotifier
{
  public:
    void notify(NotificationKind /*kind*/, std::string_view /*message*/) override {}
};

} // namespace endo
