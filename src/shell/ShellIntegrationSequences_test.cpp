// SPDX-License-Identifier: Apache-2.0
#include <core/platform/testing/TestProcessEnvironment.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>

#include "ShellIntegrationSequences.hpp"

using namespace endo::shellintegration;
using namespace std::string_view_literals;

namespace
{

TerminalQuirks quirksFor(std::map<std::string, std::string, std::less<>> entries)
{
    return detectTerminalQuirks(core::platform::testing::TestProcessEnvironment { std::move(entries) });
}

} // namespace

TEST_CASE("ShellIntegrationSequences.zones")
{
    auto const quirks = TerminalQuirks {};
    CHECK(promptZoneSequence(PromptZone::Start, quirks) == "\033]133;A;click_events=1\033\\"sv);
    CHECK(promptZoneSequence(PromptZone::Prompt, quirks) == "\033]133;P;k=i\033\\"sv);
    CHECK(promptZoneSequence(PromptZone::Continuation, quirks) == "\033]133;P;k=s\033\\"sv);
    CHECK(promptZoneSequence(PromptZone::RightPrompt, quirks) == "\033]133;P;k=r\033\\"sv);
    CHECK(promptZoneSequence(PromptZone::Input, quirks) == "\033]133;B\033\\"sv);
}

TEST_CASE("ShellIntegrationSequences.no_quirks_without_variables")
{
    auto const quirks = quirksFor({});
    CHECK_FALSE(quirks.suppressPromptParts);
    CHECK_FALSE(quirks.suppressRightPrompt);
    CHECK_FALSE(quirks.insideTmux);

    auto const empty = quirksFor({ { "KONSOLE_VERSION", "" }, { "TMUX", "" } });
    CHECK_FALSE(empty.suppressPromptParts);
    CHECK_FALSE(empty.suppressRightPrompt);
    CHECK_FALSE(empty.insideTmux);
}

TEST_CASE("ShellIntegrationSequences.konsole_suppresses_P")
{
    auto const quirks = quirksFor({ { "KONSOLE_VERSION", "1" } });
    CHECK(quirks.suppressPromptParts);
    CHECK_FALSE(quirks.insideTmux);
    CHECK(promptZoneSequence(PromptZone::Prompt, quirks).empty());
    CHECK(promptZoneSequence(PromptZone::Continuation, quirks).empty());
    CHECK(promptZoneSequence(PromptZone::RightPrompt, quirks).empty());
    CHECK(promptZoneSequence(PromptZone::Start, quirks) == "\033]133;A;click_events=1\033\\"sv);
    CHECK(promptZoneSequence(PromptZone::Input, quirks) == "\033]133;B\033\\"sv);
}

TEST_CASE("ShellIntegrationSequences.tmux_suppresses_k_r")
{
    auto const quirks = quirksFor({ { "TMUX", "tmux-socket,1234,0" } });
    CHECK(quirks.insideTmux);
    CHECK(quirks.suppressRightPrompt);
    CHECK_FALSE(quirks.suppressPromptParts);
    CHECK(promptZoneSequence(PromptZone::RightPrompt, quirks).empty());
    CHECK(promptZoneSequence(PromptZone::Continuation, quirks) == "\033]133;P;k=s\033\\"sv);
    CHECK(promptZoneSequence(PromptZone::Prompt, quirks) == "\033]133;P;k=i\033\\"sv);
}

TEST_CASE("ShellIntegrationSequences.percent_encode")
{
    CHECK(percentEncode("Az09-._~/") == "Az09-._~/");
    CHECK(percentEncode("a b") == "a%20b");
}

TEST_CASE("ShellIntegrationSequences.cmdline_hostile_bytes")
{
    // a;b ESC \ BEL LF % é(UTF-8 C3 A9) 0xFF
    auto const input = "a;b\x1b\\\a\n%\xc3\xa9"
                       "\xff"sv;
    auto const result = commandStartSequence(input);
    CHECK(result == "\033]133;C;cmdline_url=a%3Bb%1B%5C%07%0A%25%C3%A9%FF\033\\"sv);

    // The only ESC in the result is the one that starts the OSC and the one of the terminator.
    auto const escapes = std::ranges::count(result, '\033');
    CHECK(escapes == 2);
    CHECK(result.ends_with("\033\\"));
    CHECK(std::string_view { result }.substr(0, result.size() - 2).find("\033\\") == std::string_view::npos);
}

TEST_CASE("ShellIntegrationSequences.cmdline_empty")
{
    CHECK(commandStartSequence("") == "\033]133;C\033\\"sv);
}

TEST_CASE("ShellIntegrationSequences.finished")
{
    CHECK(commandFinishedSequence(130) == "\033]133;D;130\033\\"sv);
    CHECK(commandFinishedSequence(0) == "\033]133;D;0\033\\"sv);
}

TEST_CASE("ShellIntegrationSequences.current_directory")
{
    CHECK(currentDirectorySequence("/srv/x", "box") == "\033]7;file://box/srv/x\033\\"sv);
    CHECK(currentDirectorySequence("/home/me/My Docs", "") == "\033]7;file:///home/me/My%20Docs\033\\"sv);
}

TEST_CASE("ShellIntegrationSequences.user_var")
{
    CHECK(userVarSequence("WEZTERM_PROG", "vim") == "\033]1337;SetUserVar=WEZTERM_PROG=dmlt\033\\"sv);
}

TEST_CASE("ShellIntegrationSequences.tmux_passthrough")
{
    CHECK(tmuxPassthrough("\033]1337;X\033\\") == "\033Ptmux;\033\033]1337;X\033\033\\\033\\"sv);
}
