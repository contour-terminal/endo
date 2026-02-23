// SPDX-License-Identifier: Apache-2.0
#include <tui/VtParser.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace tui;

namespace
{

/// @brief Parses a VT sequence and returns the first KeyEvent, or nullopt if none.
auto parseKey(std::string_view seq) -> std::optional<KeyEvent>
{
    auto parser = VtParser {};
    auto const events = parser.feed(seq);
    for (auto const& event: events)
        if (auto const* key = std::get_if<KeyEvent>(&event))
            return *key;
    return std::nullopt;
}

} // namespace

// ============================================================================
// CSI u (Kitty keyboard protocol) tests
// ============================================================================

TEST_CASE("VtParser.CSIu.basic_character", "[tui,vtparser]")
{
    // CSI 97 u → 'a' with no modifiers
    auto const key = parseKey("\033[97u");
    REQUIRE(key.has_value());
    CHECK(key->codepoint == U'a');
    CHECK(key->modifiers == Modifier::None);
}

TEST_CASE("VtParser.CSIu.shift_3_produces_hash", "[tui,vtparser]")
{
    // CSI 51:35;2u → Shift consumed, codepoint='#'
    auto const key = parseKey("\033[51:35;2u");
    REQUIRE(key.has_value());
    CHECK(key->codepoint == U'#');
    CHECK(key->modifiers == Modifier::None);
}

TEST_CASE("VtParser.CSIu.ctrl_shift_3", "[tui,vtparser]")
{
    // CSI 51:35;6u → Shift consumed, Ctrl remains, codepoint='#'
    auto const key = parseKey("\033[51:35;6u");
    REQUIRE(key.has_value());
    CHECK(key->codepoint == U'#');
    CHECK(key->modifiers == Modifier::Ctrl);
}

TEST_CASE("VtParser.CSIu.shift_a_produces_A", "[tui,vtparser]")
{
    // CSI 97:65;2u → Shift consumed, codepoint='A'
    auto const key = parseKey("\033[97:65;2u");
    REQUIRE(key.has_value());
    CHECK(key->codepoint == U'A');
    CHECK(key->modifiers == Modifier::None);
}

TEST_CASE("VtParser.CSIu.no_shifted_key_keeps_shift", "[tui,vtparser]")
{
    // CSI 51;2u → No shifted_key, Shift preserved
    auto const key = parseKey("\033[51;2u");
    REQUIRE(key.has_value());
    CHECK(key->codepoint == U'3');
    CHECK(key->modifiers == Modifier::Shift);
}

TEST_CASE("VtParser.CSIu.plain_hash_no_shift", "[tui,vtparser]")
{
    // CSI 35u → Direct '#' key (e.g., German layout), no modifiers
    auto const key = parseKey("\033[35u");
    REQUIRE(key.has_value());
    CHECK(key->codepoint == U'#');
    CHECK(key->modifiers == Modifier::None);
}

TEST_CASE("VtParser.CSIu.shifted_key_without_shift_modifier", "[tui,vtparser]")
{
    // CSI 51:35u → shifted_key present but no Shift modifier → use base key
    auto const key = parseKey("\033[51:35u");
    REQUIRE(key.has_value());
    CHECK(key->codepoint == U'3');
    CHECK(key->modifiers == Modifier::None);
}

TEST_CASE("VtParser.CSIu.three_subparams", "[tui,vtparser]")
{
    // CSI 51:35:51;2u → key=51, shifted=35, base_layout=51, Shift consumed
    auto const key = parseKey("\033[51:35:51;2u");
    REQUIRE(key.has_value());
    CHECK(key->codepoint == U'#');
    CHECK(key->modifiers == Modifier::None);
}

TEST_CASE("VtParser.CSIu.enter_with_modifier", "[tui,vtparser]")
{
    // CSI 13;2u → Shift+Enter
    auto const key = parseKey("\033[13;2u");
    REQUIRE(key.has_value());
    CHECK(key->key == KeyCode::Enter);
    CHECK(key->modifiers == Modifier::Shift);
}

TEST_CASE("VtParser.CSIu.escape_key", "[tui,vtparser]")
{
    // CSI 27u → Escape
    auto const key = parseKey("\033[27u");
    REQUIRE(key.has_value());
    CHECK(key->key == KeyCode::Escape);
    CHECK(key->modifiers == Modifier::None);
}

// ============================================================================
// Focus event tests (DECSET 1004)
// ============================================================================

TEST_CASE("VtParser.focus.focus_in", "[tui,vtparser]")
{
    // CSI I → FocusEvent { .focused = true }
    auto parser = VtParser {};
    auto const events = parser.feed("\033[I");
    REQUIRE(events.size() == 1);
    auto const* fe = std::get_if<FocusEvent>(&events[0]);
    REQUIRE(fe != nullptr);
    CHECK(fe->focused == true);
}

TEST_CASE("VtParser.focus.focus_out", "[tui,vtparser]")
{
    // CSI O → FocusEvent { .focused = false }
    auto parser = VtParser {};
    auto const events = parser.feed("\033[O");
    REQUIRE(events.size() == 1);
    auto const* fe = std::get_if<FocusEvent>(&events[0]);
    REQUIRE(fe != nullptr);
    CHECK(fe->focused == false);
}

TEST_CASE("VtParser.focus.ss3_not_confused_with_focus_out", "[tui,vtparser]")
{
    // ESC O A → SS3 Up arrow (not focus-out)
    auto parser = VtParser {};
    auto const events = parser.feed("\033OA");
    REQUIRE(events.size() == 1);
    auto const* key = std::get_if<KeyEvent>(&events[0]);
    REQUIRE(key != nullptr);
    CHECK(key->key == KeyCode::Up);
}

TEST_CASE("VtParser.focus.focus_followed_by_key", "[tui,vtparser]")
{
    // CSI I followed by 'a' → FocusEvent then KeyEvent
    auto parser = VtParser {};
    auto const events = parser.feed("\033[Ia");
    REQUIRE(events.size() == 2);
    auto const* fe = std::get_if<FocusEvent>(&events[0]);
    REQUIRE(fe != nullptr);
    CHECK(fe->focused == true);
    auto const* key = std::get_if<KeyEvent>(&events[1]);
    REQUIRE(key != nullptr);
    CHECK(key->codepoint == U'a');
}

// ============================================================================
// DCS (Device Control String) tests
// ============================================================================

TEST_CASE("VtParser.DCS.basic_payload", "[tui,vtparser]")
{
    // ESC P hello ESC \ → DcsResponse { "hello" }
    auto parser = VtParser {};
    auto const events = parser.feed("\033Phello\033\\");
    REQUIRE(events.size() == 1);
    auto const* dcs = std::get_if<DcsResponse>(&events[0]);
    REQUIRE(dcs != nullptr);
    // The header byte 'h' transitions to body, so payload includes everything from header start.
    // DcsEntry collects params (none here), 'h' is the final byte → body collects "ello"
    // Full payload: "hello" (the 'h' is the DCS final byte, then "ello" is the body)
    CHECK(dcs->payload == "hello");
}

TEST_CASE("VtParser.DCS.empty_body", "[tui,vtparser]")
{
    // ESC P b ESC \ → DcsResponse with just the final byte 'b' as payload
    auto parser = VtParser {};
    auto const events = parser.feed("\033Pb\033\\");
    REQUIRE(events.size() == 1);
    auto const* dcs = std::get_if<DcsResponse>(&events[0]);
    REQUIRE(dcs != nullptr);
    CHECK(dcs->payload == "b");
}

TEST_CASE("VtParser.DCS.semantic_block_token_response", "[tui,vtparser]")
{
    // DCS token response: ESC P > 2034 ; 1 b TOKEN_DATA ESC backslash
    auto parser = VtParser {};
    auto const events = parser.feed("\033P>2034;1b41394;50132;58870;1816\033\\");
    REQUIRE(events.size() == 1);
    auto const* dcs = std::get_if<DcsResponse>(&events[0]);
    REQUIRE(dcs != nullptr);
    CHECK(dcs->payload == ">2034;1b41394;50132;58870;1816");
}

TEST_CASE("VtParser.DCS.query_success_json", "[tui,vtparser]")
{
    // DCS query success: ESC P > 1 b {JSON} ST
    auto const* const json =
        R"({"version":1,"command":"ls","output":"file.txt\n","exitCode":0,"finished":true})";
    auto const seq = std::string("\033P>1b") + json + "\033\\";
    auto parser = VtParser {};
    auto const events = parser.feed(seq);
    REQUIRE(events.size() == 1);
    auto const* dcs = std::get_if<DcsResponse>(&events[0]);
    REQUIRE(dcs != nullptr);
    auto const expectedPayload = std::string(">1b") + json;
    CHECK(dcs->payload == expectedPayload);
}

TEST_CASE("VtParser.DCS.error_status_no_data", "[tui,vtparser]")
{
    // DCS status 0 (no data): ESC P > 0 b ST
    auto parser = VtParser {};
    auto const events = parser.feed("\033P>0b\033\\");
    REQUIRE(events.size() == 1);
    auto const* dcs = std::get_if<DcsResponse>(&events[0]);
    REQUIRE(dcs != nullptr);
    CHECK(dcs->payload == ">0b");
}

TEST_CASE("VtParser.DCS.error_status_auth_required", "[tui,vtparser]")
{
    // DCS status 2 (auth required): ESC P > 2 b ST
    auto parser = VtParser {};
    auto const events = parser.feed("\033P>2b\033\\");
    REQUIRE(events.size() == 1);
    auto const* dcs = std::get_if<DcsResponse>(&events[0]);
    REQUIRE(dcs != nullptr);
    CHECK(dcs->payload == ">2b");
}

TEST_CASE("VtParser.DCS.followed_by_normal_input", "[tui,vtparser]")
{
    // DCS followed by normal key input — state recovery to Ground
    auto parser = VtParser {};
    auto const events = parser.feed("\033Phello\033\\abc");
    REQUIRE(events.size() == 4); // DcsResponse + 3 KeyEvents
    auto const* dcs = std::get_if<DcsResponse>(&events[0]);
    REQUIRE(dcs != nullptr);
    CHECK(dcs->payload == "hello");
    // Verify subsequent keys parsed correctly
    auto const* keyA = std::get_if<KeyEvent>(&events[1]);
    REQUIRE(keyA != nullptr);
    CHECK(keyA->codepoint == U'a');
    auto const* keyB = std::get_if<KeyEvent>(&events[2]);
    REQUIRE(keyB != nullptr);
    CHECK(keyB->codepoint == U'b');
    auto const* keyC = std::get_if<KeyEvent>(&events[3]);
    REQUIRE(keyC != nullptr);
    CHECK(keyC->codepoint == U'c');
}

TEST_CASE("VtParser.DCS.incremental_feed", "[tui,vtparser]")
{
    // Feed DCS in multiple chunks
    auto parser = VtParser {};
    auto events = parser.feed("\033P>1b");
    CHECK(events.empty()); // Not complete yet
    events = parser.feed("payload");
    CHECK(events.empty()); // Still collecting
    events = parser.feed("\033\\");
    REQUIRE(events.size() == 1);
    auto const* dcs = std::get_if<DcsResponse>(&events[0]);
    REQUIRE(dcs != nullptr);
    CHECK(dcs->payload == ">1bpayload");
}

// ============================================================================
// DECRQM (DEC Request Mode) response tests
// ============================================================================

TEST_CASE("VtParser.DECRQM.mode_set", "[tui,vtparser]")
{
    // CSI ? 2034 ; 1 $ y → DecModeReport { 2034, 1 }
    auto parser = VtParser {};
    auto const events = parser.feed("\033[?2034;1$y");
    REQUIRE(events.size() == 1);
    auto const* report = std::get_if<DecModeReport>(&events[0]);
    REQUIRE(report != nullptr);
    CHECK(report->mode == 2034);
    CHECK(report->status == 1);
}

TEST_CASE("VtParser.DECRQM.mode_not_recognized", "[tui,vtparser]")
{
    // CSI ? 2034 ; 0 $ y → DecModeReport { 2034, 0 }
    auto parser = VtParser {};
    auto const events = parser.feed("\033[?2034;0$y");
    REQUIRE(events.size() == 1);
    auto const* report = std::get_if<DecModeReport>(&events[0]);
    REQUIRE(report != nullptr);
    CHECK(report->mode == 2034);
    CHECK(report->status == 0);
}

TEST_CASE("VtParser.DECRQM.mode_reset", "[tui,vtparser]")
{
    // CSI ? 2034 ; 2 $ y → DecModeReport { 2034, 2 }
    auto parser = VtParser {};
    auto const events = parser.feed("\033[?2034;2$y");
    REQUIRE(events.size() == 1);
    auto const* report = std::get_if<DecModeReport>(&events[0]);
    REQUIRE(report != nullptr);
    CHECK(report->mode == 2034);
    CHECK(report->status == 2);
}

TEST_CASE("VtParser.DECRQM.different_mode", "[tui,vtparser]")
{
    // CSI ? 1004 ; 1 $ y → DecModeReport { 1004, 1 } (focus reporting set)
    auto parser = VtParser {};
    auto const events = parser.feed("\033[?1004;1$y");
    REQUIRE(events.size() == 1);
    auto const* report = std::get_if<DecModeReport>(&events[0]);
    REQUIRE(report != nullptr);
    CHECK(report->mode == 1004);
    CHECK(report->status == 1);
}
