// SPDX-License-Identifier: Apache-2.0

#include <tui/InputEvent.hpp>
#include <tui/TerminalInput.hpp>

#include <catch2/catch_test_macros.hpp>

#include <fcntl.h>
#include <unistd.h>

using tui::KeyEvent;
using tui::TerminalInput;

namespace
{

/// RAII: point STDIN_FILENO at /dev/null for the scope, so TerminalInput::readReadyInput
/// (which reads _fd == STDIN) sees EOF and adds no spurious live events — isolating the
/// pushed-back events. Restores the original stdin on destruction.
class RedirectStdinToNull
{
  public:
    RedirectStdinToNull(): _saved(::dup(STDIN_FILENO))
    {
        auto const devnull = ::open("/dev/null", O_RDONLY);
        if (devnull >= 0)
        {
            ::dup2(devnull, STDIN_FILENO);
            ::close(devnull);
        }
    }

    ~RedirectStdinToNull()
    {
        if (_saved >= 0)
        {
            ::dup2(_saved, STDIN_FILENO);
            ::close(_saved);
        }
    }

    RedirectStdinToNull(RedirectStdinToNull const&) = delete;
    RedirectStdinToNull& operator=(RedirectStdinToNull const&) = delete;
    RedirectStdinToNull(RedirectStdinToNull&&) = delete;
    RedirectStdinToNull& operator=(RedirectStdinToNull&&) = delete;

  private:
    int _saved;
};

} // namespace

TEST_CASE("TerminalInput.pushBack_restages_bytes_as_events")
{
    RedirectStdinToNull const guard;

    TerminalInput input;

    // Re-stage the bytes a completion abort-watcher would have read off the TTY.
    input.pushBack("ab");

    // readReadyInput must return the pushed-back, parser-decoded events (STDIN is /dev/null
    // so no live input is mixed in).
    auto const events = input.readReadyInput();

    REQUIRE(events.size() == 2);
    auto const* k0 = std::get_if<KeyEvent>(events.data());
    auto const* k1 = std::get_if<KeyEvent>(events.data() + 1);
    REQUIRE(k0 != nullptr);
    REQUIRE(k1 != nullptr);
    CHECK(k0->codepoint == U'a');
    CHECK(k1->codepoint == U'b');
}

TEST_CASE("TerminalInput.pushBack_is_drained_once")
{
    RedirectStdinToNull const guard;

    TerminalInput input;
    input.pushBack("x");

    auto const first = input.readReadyInput();
    REQUIRE(first.size() == 1);

    // A second read must not replay the already-consumed pushback.
    auto const second = input.readReadyInput();
    CHECK(second.empty());
}

TEST_CASE("TerminalInput.pushBack_empty_is_noop")
{
    RedirectStdinToNull const guard;

    TerminalInput input;
    input.pushBack("");

    auto const events = input.readReadyInput();
    CHECK(events.empty());
}
