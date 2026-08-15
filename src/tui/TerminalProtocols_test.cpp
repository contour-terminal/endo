// SPDX-License-Identifier: Apache-2.0
#include <tui/TerminalProtocols.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace tui::protocols;

// ============================================================================
// OSC 8 hyperlink sequences
// ============================================================================

TEST_CASE("TerminalProtocols.buildHyperlinkOpen_byte_exact")
{
    CHECK(buildHyperlinkOpen("https://x.com") == "\033]8;;https://x.com\033\\");
}

TEST_CASE("TerminalProtocols.buildHyperlinkOpen_empty_url")
{
    CHECK(buildHyperlinkOpen("") == "\033]8;;\033\\");
}

TEST_CASE("TerminalProtocols.hyperlinkClose_byte_exact")
{
    CHECK(std::string(HyperlinkClose) == "\033]8;;\033\\");
}

TEST_CASE("TerminalProtocols.hyperlink_round_trip")
{
    auto const sequence = buildHyperlinkOpen("https://a.b/c?d=1#e") + "label" + std::string(HyperlinkClose);
    CHECK(sequence == "\033]8;;https://a.b/c?d=1#e\033\\label\033]8;;\033\\");
}

TEST_CASE("TerminalProtocols.buildHyperlinkOpen_with_id_byte_exact")
{
    CHECK(buildHyperlinkOpen("file:///a", "endo-1f2e") == "\033]8;id=endo-1f2e;file:///a\033\\");
}

TEST_CASE("TerminalProtocols.buildHyperlinkOpen_empty_id_matches_id_less_form")
{
    // An empty id must emit no parameters at all, so callers that do not care about link
    // identity keep producing the exact bytes the id-less overload always produced.
    CHECK(buildHyperlinkOpen("https://x.com", "") == buildHyperlinkOpen("https://x.com"));
    CHECK(buildHyperlinkOpen("https://x.com", "") == "\033]8;;https://x.com\033\\");
}

// ============================================================================
// DA1 (Primary Device Attributes) Sixel detection
// ============================================================================

TEST_CASE("TerminalProtocols.da1_reports_sixel_when_parameter_4_present")
{
    CHECK(parseSixelFromDeviceAttributes("\033[?62;4;6c"));
    CHECK(parseSixelFromDeviceAttributes("\033[?63;4c"));
    CHECK(parseSixelFromDeviceAttributes("\033[?4c"));
    CHECK(parseSixelFromDeviceAttributes("\033[?62;1;2;4;6;9;15;22c"));
}

TEST_CASE("TerminalProtocols.da1_reports_no_sixel_when_parameter_4_absent")
{
    CHECK_FALSE(parseSixelFromDeviceAttributes("\033[?62;6c"));
    CHECK_FALSE(parseSixelFromDeviceAttributes("\033[?1;2c"));
    CHECK_FALSE(parseSixelFromDeviceAttributes("\033[?62c"));
}

TEST_CASE("TerminalProtocols.da1_does_not_match_parameter_substrings")
{
    // 14, 40 and 64 all contain '4' but are not the Sixel attribute.
    CHECK_FALSE(parseSixelFromDeviceAttributes("\033[?14;40;64c"));
}

TEST_CASE("TerminalProtocols.da1_accepts_single_byte_csi")
{
    CHECK(parseSixelFromDeviceAttributes("\x9B?62;4c"));
    CHECK_FALSE(parseSixelFromDeviceAttributes("\x9B?62c"));
}

TEST_CASE("TerminalProtocols.da1_rejects_malformed_responses")
{
    CHECK_FALSE(parseSixelFromDeviceAttributes(""));
    CHECK_FALSE(parseSixelFromDeviceAttributes("garbage"));
    CHECK_FALSE(parseSixelFromDeviceAttributes("\033[?62;4")); // truncated: no terminating 'c'
    CHECK_FALSE(parseSixelFromDeviceAttributes("\033[62;4c")); // missing '?' private marker
    CHECK_FALSE(parseSixelFromDeviceAttributes("4"));
}

TEST_CASE("TerminalProtocols.da1_tolerates_surrounding_noise")
{
    // Other terminal replies may arrive in the same read.
    CHECK(parseSixelFromDeviceAttributes("\033[1;1R\033[?62;4;6c"));
}
