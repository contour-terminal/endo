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
