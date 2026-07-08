// SPDX-License-Identifier: Apache-2.0
#include <shell/TTY.hpp>
#include <shell/TerminalCompletionNotifier.hpp>

#include <format>

namespace endo
{

void TerminalCompletionNotifier::notify(NotificationKind /*kind*/, std::string_view message)
{
    // A single dim line on stderr: dim so it reads as chrome rather than output,
    // stderr so it never contaminates a captured command-substitution result. The
    // leading newline lifts it off the current prompt line.
    _tty->writeToStderr(std::format("\n\x1b[2m{}\x1b[0m\n", message));
}

} // namespace endo
