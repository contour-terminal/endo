// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// Default @c CompletionNotifier that writes a one-line notice to the terminal.

#include <shell/CompletionNotifier.hpp>

#include <string_view>

namespace endo
{

class TTY;

/// Writes completion-wait notifications as a single dim line to the terminal's
/// stderr (so it does not interleave with captured stdout), e.g.
/// `completion timed out after 3s`. This is the interactive default; swap in a
/// different @c CompletionNotifier for GUI popups, system notifications, or to
/// suppress the notice entirely.
class TerminalCompletionNotifier final: public CompletionNotifier
{
  public:
    /// @param tty The terminal to write notices to (not owned; must outlive this).
    explicit TerminalCompletionNotifier(TTY const& tty) noexcept: _tty(&tty) {}

    void notify(NotificationKind kind, std::string_view message) override;

  private:
    TTY const* _tty; ///< Borrowed terminal sink.
};

} // namespace endo
