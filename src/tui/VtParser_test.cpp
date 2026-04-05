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
// CSI u numpad key tests
// ============================================================================

TEST_CASE("VtParser.CSIu.numpad_5_with_numlock", "[tui,vtparser]")
{
    // CSI 57404;129u → KP_5 with NumLock (modifier param 129 = 1 + 128)
    auto const key = parseKey("\033[57404;129u");
    REQUIRE(key.has_value());
    CHECK(key->key == KeyCode::Kp5);
    CHECK(key->codepoint == U'5');
    CHECK(hasModifier(key->modifiers, Modifier::NumLock));
}

TEST_CASE("VtParser.CSIu.numpad_5_without_numlock", "[tui,vtparser]")
{
    // CSI 57404u → KP_5 without NumLock — no text character
    auto const key = parseKey("\033[57404u");
    REQUIRE(key.has_value());
    CHECK(key->key == KeyCode::Kp5);
    CHECK(key->codepoint == 0);
}

TEST_CASE("VtParser.CSIu.numpad_0_with_numlock", "[tui,vtparser]")
{
    // CSI 57399;129u → KP_0 with NumLock
    auto const key = parseKey("\033[57399;129u");
    REQUIRE(key.has_value());
    CHECK(key->key == KeyCode::Kp0);
    CHECK(key->codepoint == U'0');
}

TEST_CASE("VtParser.CSIu.numpad_add_no_numlock", "[tui,vtparser]")
{
    // CSI 57413u → KP_Add without NumLock — operators always produce text
    auto const key = parseKey("\033[57413u");
    REQUIRE(key.has_value());
    CHECK(key->key == KeyCode::KpAdd);
    CHECK(key->codepoint == U'+');
}

TEST_CASE("VtParser.CSIu.numpad_decimal_with_numlock", "[tui,vtparser]")
{
    // CSI 57409;129u → KP_Decimal with NumLock
    auto const key = parseKey("\033[57409;129u");
    REQUIRE(key.has_value());
    CHECK(key->key == KeyCode::KpDecimal);
    CHECK(key->codepoint == U'.');
}

TEST_CASE("VtParser.CSIu.numpad_home_no_text", "[tui,vtparser]")
{
    // CSI 57423;129u → KP_Home with NumLock — navigation keys never produce text
    auto const key = parseKey("\033[57423;129u");
    REQUIRE(key.has_value());
    CHECK(key->key == KeyCode::KpHome);
    CHECK(key->codepoint == 0);
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
    auto const* fe = std::get_if<FocusEvent>(events.data());
    REQUIRE(fe != nullptr);
    CHECK(fe->focused == true);
}

TEST_CASE("VtParser.focus.focus_out", "[tui,vtparser]")
{
    // CSI O → FocusEvent { .focused = false }
    auto parser = VtParser {};
    auto const events = parser.feed("\033[O");
    REQUIRE(events.size() == 1);
    auto const* fe = std::get_if<FocusEvent>(events.data());
    REQUIRE(fe != nullptr);
    CHECK(fe->focused == false);
}

TEST_CASE("VtParser.focus.ss3_not_confused_with_focus_out", "[tui,vtparser]")
{
    // ESC O A → SS3 Up arrow (not focus-out)
    auto parser = VtParser {};
    auto const events = parser.feed("\033OA");
    REQUIRE(events.size() == 1);
    auto const* key = std::get_if<KeyEvent>(events.data());
    REQUIRE(key != nullptr);
    CHECK(key->key == KeyCode::Up);
}

TEST_CASE("VtParser.focus.focus_followed_by_key", "[tui,vtparser]")
{
    // CSI I followed by 'a' → FocusEvent then KeyEvent
    auto parser = VtParser {};
    auto const events = parser.feed("\033[Ia");
    REQUIRE(events.size() == 2);
    auto const* fe = std::get_if<FocusEvent>(events.data());
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
    auto const* dcs = std::get_if<DcsResponse>(events.data());
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
    auto const* dcs = std::get_if<DcsResponse>(events.data());
    REQUIRE(dcs != nullptr);
    CHECK(dcs->payload == "b");
}

TEST_CASE("VtParser.DCS.semantic_block_token_response", "[tui,vtparser]")
{
    // DCS token response: ESC P > 2034 ; 1 b TOKEN_DATA ESC backslash
    auto parser = VtParser {};
    auto const events = parser.feed("\033P>2034;1b41394;50132;58870;1816\033\\");
    REQUIRE(events.size() == 1);
    auto const* dcs = std::get_if<DcsResponse>(events.data());
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
    auto const* dcs = std::get_if<DcsResponse>(events.data());
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
    auto const* dcs = std::get_if<DcsResponse>(events.data());
    REQUIRE(dcs != nullptr);
    CHECK(dcs->payload == ">0b");
}

TEST_CASE("VtParser.DCS.error_status_auth_required", "[tui,vtparser]")
{
    // DCS status 2 (auth required): ESC P > 2 b ST
    auto parser = VtParser {};
    auto const events = parser.feed("\033P>2b\033\\");
    REQUIRE(events.size() == 1);
    auto const* dcs = std::get_if<DcsResponse>(events.data());
    REQUIRE(dcs != nullptr);
    CHECK(dcs->payload == ">2b");
}

TEST_CASE("VtParser.DCS.followed_by_normal_input", "[tui,vtparser]")
{
    // DCS followed by normal key input — state recovery to Ground
    auto parser = VtParser {};
    auto const events = parser.feed("\033Phello\033\\abc");
    REQUIRE(events.size() == 4); // DcsResponse + 3 KeyEvents
    auto const* dcs = std::get_if<DcsResponse>(events.data());
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
    auto const* dcs = std::get_if<DcsResponse>(events.data());
    REQUIRE(dcs != nullptr);
    CHECK(dcs->payload == ">1bpayload");
}

// ============================================================================
// Win32 input mode (CSI Vk;Sc;Uc;Kd;Cs;Rc _) tests
// ============================================================================

TEST_CASE("VtParser.Win32Input.basic_printable_a", "[tui,vtparser]")
{
    // VK_A=0x41, SC=0x1E, UC='a'=97, Kd=1, CS=0, Rc=1
    auto const key = parseKey("\033[65;30;97;1;0;1_");
    REQUIRE(key.has_value());
    CHECK(key->codepoint == U'a');
    CHECK(key->modifiers == Modifier::None);
}

TEST_CASE("VtParser.Win32Input.keyup_ignored", "[tui,vtparser]")
{
    // Kd=0 → key-up, should be ignored
    auto parser = VtParser {};
    auto const events = parser.feed("\033[65;30;97;0;0;1_");
    CHECK(events.empty());
}

TEST_CASE("VtParser.Win32Input.ctrl_space", "[tui,vtparser]")
{
    // VK_SPACE=0x20, UC=0 (NUL due to Ctrl), CS=LEFT_CTRL(0x08)
    auto const key = parseKey("\033[32;57;0;1;8;1_");
    REQUIRE(key.has_value());
    CHECK(key->codepoint == U' ');
    CHECK(key->modifiers == Modifier::Ctrl);
}

TEST_CASE("VtParser.Win32Input.ctrl_backspace", "[tui,vtparser]")
{
    // VK_BACK=0x08, CS=LEFT_CTRL(0x08)
    auto const key = parseKey("\033[8;14;127;1;8;1_");
    REQUIRE(key.has_value());
    CHECK(key->key == KeyCode::Backspace);
    CHECK(key->modifiers == Modifier::Ctrl);
}

TEST_CASE("VtParser.Win32Input.ctrl_left_arrow", "[tui,vtparser]")
{
    // VK_LEFT=0x25, CS=LEFT_CTRL(0x08)
    auto const key = parseKey("\033[37;75;0;1;8;1_");
    REQUIRE(key.has_value());
    CHECK(key->key == KeyCode::Left);
    CHECK(key->modifiers == Modifier::Ctrl);
}

TEST_CASE("VtParser.Win32Input.f5_no_modifiers", "[tui,vtparser]")
{
    // VK_F5=0x74
    auto const key = parseKey("\033[116;63;0;1;0;1_");
    REQUIRE(key.has_value());
    CHECK(key->key == KeyCode::F5);
    CHECK(key->modifiers == Modifier::None);
}

TEST_CASE("VtParser.Win32Input.shift_alt_enter", "[tui,vtparser]")
{
    // VK_RETURN=0x0D, CS=SHIFT(0x10)|LEFT_ALT(0x02)=0x12
    auto const key = parseKey("\033[13;28;13;1;18;1_");
    REQUIRE(key.has_value());
    CHECK(key->key == KeyCode::Enter);
    CHECK(key->modifiers == (Modifier::Shift | Modifier::Alt));
}

TEST_CASE("VtParser.Win32Input.modifier_only_ctrl_filtered", "[tui,vtparser]")
{
    // VK_CONTROL=0x11 (bare Ctrl press) — should be filtered
    auto parser = VtParser {};
    auto const events = parser.feed("\033[17;29;0;1;8;1_");
    CHECK(events.empty());
}

TEST_CASE("VtParser.Win32Input.modifier_only_shift_filtered", "[tui,vtparser]")
{
    // VK_SHIFT=0x10 (bare Shift press) — should be filtered
    auto parser = VtParser {};
    auto const events = parser.feed("\033[16;42;0;1;16;1_");
    CHECK(events.empty());
}

TEST_CASE("VtParser.Win32Input.ctrl_a", "[tui,vtparser]")
{
    // VK_A=0x41, CS=LEFT_CTRL(0x08) → key='a', Ctrl
    auto const key = parseKey("\033[65;30;1;1;8;1_");
    REQUIRE(key.has_value());
    CHECK(key->codepoint == U'a');
    CHECK(key->modifiers == Modifier::Ctrl);
}

TEST_CASE("VtParser.Win32Input.digit_3", "[tui,vtparser]")
{
    // VK '3'=0x33, UC='3'=51, no modifiers
    auto const key = parseKey("\033[51;4;51;1;0;1_");
    REQUIRE(key.has_value());
    CHECK(key->codepoint == U'3');
    CHECK(key->modifiers == Modifier::None);
}

TEST_CASE("VtParser.Win32Input.shift_9_parenthesis", "[tui,vtparser]")
{
    // Shift+9 on US layout: VK '9'=0x39=57, UC='('=40, CS=SHIFT(0x10)=16
    auto const key = parseKey("\033[57;10;40;1;16;1_");
    REQUIRE(key.has_value());
    CHECK(key->codepoint == U'(');
    CHECK(key->key == KeyCode { U'(' });
}

TEST_CASE("VtParser.Win32Input.shift_0_parenthesis", "[tui,vtparser]")
{
    // Shift+0 on US layout: VK '0'=0x30=48, UC=')'=41, CS=SHIFT(0x10)=16
    auto const key = parseKey("\033[48;11;41;1;16;1_");
    REQUIRE(key.has_value());
    CHECK(key->codepoint == U')');
    CHECK(key->key == KeyCode { U')' });
}

TEST_CASE("VtParser.Win32Input.shift_1_exclamation", "[tui,vtparser]")
{
    // Shift+1 on US layout: VK '1'=0x31=49, UC='!'=33, CS=SHIFT(0x10)=16
    auto const key = parseKey("\033[49;2;33;1;16;1_");
    REQUIRE(key.has_value());
    CHECK(key->codepoint == U'!');
    CHECK(key->key == KeyCode { U'!' });
}

TEST_CASE("VtParser.Win32Input.optional_params_with_defaults", "[tui,vtparser]")
{
    // 4 params: Vk=65(A), Sc=30, Uc=97('a'), Kd=1 — Cs defaults to 0, Rc defaults to 1
    auto const key = parseKey("\033[65;30;97;1_");
    REQUIRE(key.has_value());
    CHECK(key->codepoint == U'a');
    CHECK(key->modifiers == Modifier::None);
}

TEST_CASE("VtParser.Win32Input.zero_params_ignored", "[tui,vtparser]")
{
    // Zero params (just the final byte) — empty, should be ignored
    auto parser = VtParser {};
    auto const events = parser.feed("\033[_");
    CHECK(events.empty());
}

TEST_CASE("VtParser.Win32Input.three_params_keyup_default", "[tui,vtparser]")
{
    // 3 params: Vk=65, Sc=30, Uc=97 — Kd defaults to 0 (key-up), filtered
    auto parser = VtParser {};
    auto const events = parser.feed("\033[65;30;97_");
    CHECK(events.empty());
}

TEST_CASE("VtParser.Win32Input.followed_by_normal_input", "[tui,vtparser]")
{
    // Win32 input sequence followed by normal 'x' — state recovery
    auto parser = VtParser {};
    auto const events = parser.feed("\033[65;30;97;1;0;1_x");
    REQUIRE(events.size() == 2);
    auto const* key1 = std::get_if<KeyEvent>(events.data());
    REQUIRE(key1 != nullptr);
    CHECK(key1->codepoint == U'a');
    auto const* key2 = std::get_if<KeyEvent>(&events[1]);
    REQUIRE(key2 != nullptr);
    CHECK(key2->codepoint == U'x');
}

TEST_CASE("VtParser.Win32Input.capslock_numlock_modifiers", "[tui,vtparser]")
{
    // VK_A, CS=CAPSLOCK(0x80)|NUMLOCK(0x20)=0xA0
    auto const key = parseKey("\033[65;30;65;1;160;1_");
    REQUIRE(key.has_value());
    CHECK(key->codepoint == U'a');
    CHECK(hasModifier(key->modifiers, Modifier::CapsLock));
    CHECK(hasModifier(key->modifiers, Modifier::NumLock));
}

TEST_CASE("VtParser.Win32Input.shift_a", "[tui,vtparser]")
{
    // VK_A=0x41, UC='A'=65, CS=SHIFT(0x10) → key='a', Shift
    auto const key = parseKey("\033[65;30;65;1;16;1_");
    REQUIRE(key.has_value());
    CHECK(key->codepoint == U'a');
    CHECK(key->modifiers == Modifier::Shift);
}

TEST_CASE("VtParser.Win32Input.altgr_suppresses_ctrl_alt", "[tui,vtparser]")
{
    // AltGr sends RightAlt(0x01)|LeftCtrl(0x08)=0x09. Should produce no Alt/Ctrl modifiers.
    // VK_E=0x45, UC=U+20AC '€' (8364 decimal) — AltGr+E on German keyboard.
    // The handler should emit the composed Unicode character from unicodeChar, not the base letter.
    auto const key = parseKey("\033[69;18;8364;1;9;1_");
    REQUIRE(key.has_value());
    CHECK(key->codepoint == U'\u20AC'); // Euro sign '€'
    CHECK(key->modifiers == Modifier::None);
}

TEST_CASE("VtParser.Win32Input.left_win_produces_super", "[tui,vtparser]")
{
    // VK_A=0x41, CS=LeftWin(0x0400) → Super modifier
    auto const key = parseKey("\033[65;30;97;1;1024;1_");
    REQUIRE(key.has_value());
    CHECK(key->codepoint == U'a');
    CHECK(hasModifier(key->modifiers, Modifier::Super));
}

TEST_CASE("VtParser.Win32Input.right_win_produces_super", "[tui,vtparser]")
{
    // VK_A=0x41, CS=RightWin(0x0200) → Super modifier
    auto const key = parseKey("\033[65;30;97;1;512;1_");
    REQUIRE(key.has_value());
    CHECK(key->codepoint == U'a');
    CHECK(hasModifier(key->modifiers, Modifier::Super));
}

TEST_CASE("VtParser.Win32Input.win_ctrl_combo", "[tui,vtparser]")
{
    // VK_A=0x41, CS=LeftWin(0x0400)|LeftCtrl(0x0008)=0x0408=1032
    auto const key = parseKey("\033[65;30;97;1;1032;1_");
    REQUIRE(key.has_value());
    CHECK(key->codepoint == U'a');
    CHECK(hasModifier(key->modifiers, Modifier::Super));
    CHECK(hasModifier(key->modifiers, Modifier::Ctrl));
}

TEST_CASE("VtParser.Win32Input.altgr_with_extra_modifiers", "[tui,vtparser]")
{
    // AltGr(RightAlt|LeftCtrl) with LeftAlt also set: 0x01|0x08|0x02=0x0B=11
    // Reference only checks RightAlt && LeftCtrl → still AltGr.
    // VK_E=0x45, UC=U+20AC '€'
    auto const key = parseKey("\033[69;18;8364;1;11;1_");
    REQUIRE(key.has_value());
    CHECK(key->codepoint == U'\u20AC');
    // AltGr suppresses Alt+Ctrl, so no modifiers expected
    CHECK(key->modifiers == Modifier::None);
}

TEST_CASE("VtParser.Win32Input.numpad_5", "[tui,vtparser]")
{
    // VK_NUMPAD5=0x65=101, no modifiers (no NumLock)
    auto const key = parseKey("\033[101;76;53;1;0;1_");
    REQUIRE(key.has_value());
    CHECK(key->key == KeyCode::Kp5);
    CHECK(key->modifiers == Modifier::None);
    CHECK(key->codepoint == 0);
}

TEST_CASE("VtParser.Win32Input.ctrl_numpad_0", "[tui,vtparser]")
{
    // VK_NUMPAD0=0x60=96, CS=LeftCtrl(0x08), no NumLock
    auto const key = parseKey("\033[96;82;48;1;8;1_");
    REQUIRE(key.has_value());
    CHECK(key->key == KeyCode::Kp0);
    CHECK(key->modifiers == Modifier::Ctrl);
    CHECK(key->codepoint == 0);
}

TEST_CASE("VtParser.Win32Input.numpad_multiply", "[tui,vtparser]")
{
    // VK_MULTIPLY=0x6A=106, no NumLock — operators always produce text
    auto const key = parseKey("\033[106;55;42;1;0;1_");
    REQUIRE(key.has_value());
    CHECK(key->key == KeyCode::KpMultiply);
    CHECK(key->modifiers == Modifier::None);
    CHECK(key->codepoint == U'*');
}

TEST_CASE("VtParser.Win32Input.numpad_5_with_numlock", "[tui,vtparser]")
{
    // VK_NUMPAD5=0x65=101, UC=53 ('5'), CS=NumLock(0x20)
    auto const key = parseKey("\033[101;76;53;1;32;1_");
    REQUIRE(key.has_value());
    CHECK(key->key == KeyCode::Kp5);
    CHECK(key->codepoint == U'5');
    CHECK(hasModifier(key->modifiers, Modifier::NumLock));
}

TEST_CASE("VtParser.Win32Input.numpad_add_always_text", "[tui,vtparser]")
{
    // VK_ADD=0x6B=107, UC=43 ('+'), CS=0 — operators always produce text
    auto const key = parseKey("\033[107;78;43;1;0;1_");
    REQUIRE(key.has_value());
    CHECK(key->key == KeyCode::KpAdd);
    CHECK(key->codepoint == U'+');
}

TEST_CASE("VtParser.Win32Input.altgr_digit_composed", "[tui,vtparser]")
{
    // AltGr+2 on German keyboard: VK '2'=0x32=50, UC=U+00B2 '²'=178,
    // CS=RightAlt(0x01)|LeftCtrl(0x08)=0x09 (AltGr)
    auto const key = parseKey("\033[50;3;178;1;9;1_");
    REQUIRE(key.has_value());
    CHECK(key->codepoint == U'\u00B2'); // superscript 2 '²'
    CHECK(key->modifiers == Modifier::None);
}

TEST_CASE("VtParser.Win32Input.unicode_fallback_semicolon", "[tui,vtparser]")
{
    // VK_OEM_1=0xBA=186 (';:' key on US layout), UC=';'=59, no modifiers.
    // Not A-Z, not 0-9, not a special key — falls through to unicodeChar >= 32 path.
    auto const key = parseKey("\033[186;39;59;1;0;1_");
    REQUIRE(key.has_value());
    CHECK(key->codepoint == U';');
    CHECK(key->modifiers == Modifier::None);
}

TEST_CASE("VtParser.Win32Input.space_no_modifiers", "[tui,vtparser]")
{
    // VK_SPACE=0x20, UC=' '=32, no modifiers
    auto const key = parseKey("\033[32;57;32;1;0;1_");
    REQUIRE(key.has_value());
    CHECK(key->codepoint == U' ');
    CHECK(key->modifiers == Modifier::None);
}

TEST_CASE("VtParser.Win32Input.single_param_vk_zero", "[tui,vtparser]")
{
    // Single param Vk=0 — Kd defaults to 0 (key-up), should be filtered
    auto parser = VtParser {};
    auto const events = parser.feed("\033[0_");
    CHECK(events.empty());
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
    auto const* report = std::get_if<DecModeReport>(events.data());
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
    auto const* report = std::get_if<DecModeReport>(events.data());
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
    auto const* report = std::get_if<DecModeReport>(events.data());
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
    auto const* report = std::get_if<DecModeReport>(events.data());
    REQUIRE(report != nullptr);
    CHECK(report->mode == 1004);
    CHECK(report->status == 1);
}

// ============================================================================
// Bracketed paste tests
// ============================================================================

TEST_CASE("VtParser.Paste.basic_text", "[tui,vtparser]")
{
    // Basic bracketed paste: ESC[200~ text ESC[201~
    auto parser = VtParser {};
    auto const events = parser.feed("\033[200~hello world\033[201~");
    REQUIRE(events.size() == 1);
    auto const* paste = std::get_if<PasteEvent>(events.data());
    REQUIRE(paste != nullptr);
    CHECK(paste->text == "hello world");
}

TEST_CASE("VtParser.Paste.multiline_raw_newlines", "[tui,vtparser]")
{
    // Multi-line paste with raw newlines (no Win32 input mode sequences)
    auto parser = VtParser {};
    auto const events = parser.feed("\033[200~line1\nline2\nline3\033[201~");
    REQUIRE(events.size() == 1);
    auto const* paste = std::get_if<PasteEvent>(events.data());
    REQUIRE(paste != nullptr);
    CHECK(paste->text == "line1\nline2\nline3");
}

TEST_CASE("VtParser.Paste.win32_enter_key_replaced_with_newline", "[tui,vtparser]")
{
    // Windows Terminal sends Enter as Win32 input mode sequences inside bracketed paste.
    // Key-down: CSI 13;28;13;1;0;1 _ → should become '\n'
    // Key-up:   CSI 13;28;13;0;0;1 _ → should be stripped
    auto parser = VtParser {};
    auto const events = parser.feed("\033[200~"
                                    "let name = \"world\""
                                    "\033[13;28;13;1;0;1_" // Enter key-down
                                    "\033[13;28;13;0;0;1_" // Enter key-up
                                    "println $\"Hello, {name}!\""
                                    "\033[201~");
    REQUIRE(events.size() == 1);
    auto const* paste = std::get_if<PasteEvent>(events.data());
    REQUIRE(paste != nullptr);
    CHECK(paste->text == "let name = \"world\"\nprintln $\"Hello, {name}!\"");
}

TEST_CASE("VtParser.Paste.win32_keyup_events_stripped", "[tui,vtparser]")
{
    // Key-up events (Kd=0) within paste should be silently removed
    auto parser = VtParser {};
    auto const events = parser.feed("\033[200~"
                                    "abc"
                                    "\033[65;30;97;0;0;1_" // 'a' key-up (Kd=0)
                                    "def"
                                    "\033[201~");
    REQUIRE(events.size() == 1);
    auto const* paste = std::get_if<PasteEvent>(events.data());
    REQUIRE(paste != nullptr);
    CHECK(paste->text == "abcdef");
}

TEST_CASE("VtParser.Paste.win32_modifier_only_stripped", "[tui,vtparser]")
{
    // Modifier-only keys (unicodeChar=0) within paste should be silently removed
    auto parser = VtParser {};
    auto const events = parser.feed("\033[200~"
                                    "text"
                                    "\033[16;42;0;1;0;1_" // Shift key-down (Vk=0x10, Uc=0)
                                    "\033[201~");
    REQUIRE(events.size() == 1);
    auto const* paste = std::get_if<PasteEvent>(events.data());
    REQUIRE(paste != nullptr);
    CHECK(paste->text == "text");
}

TEST_CASE("VtParser.Paste.win32_printable_chars_replaced", "[tui,vtparser]")
{
    // Win32 input mode sequences for printable chars should be replaced with the character
    auto parser = VtParser {};
    auto const events = parser.feed("\033[200~"
                                    "before"
                                    "\033[9;15;9;1;0;1_" // Tab key-down (Vk=9, Uc=9)
                                    "after"
                                    "\033[201~");
    REQUIRE(events.size() == 1);
    auto const* paste = std::get_if<PasteEvent>(events.data());
    REQUIRE(paste != nullptr);
    CHECK(paste->text == "before\tafter");
}
