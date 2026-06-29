// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "PromptPresets.hpp"

using namespace std::string_view_literals;

TEST_CASE("promptPreset.endo_signature_defaults_to_transparent_background", "[prompt-presets]")
{
    auto const config = endo::promptPreset("endo-signature"sv);
    CHECK(config.name == "endo-signature"sv);
    CHECK(config.colorOverrides.transparentBackground);
    CHECK_FALSE(config.colorOverrides.background.has_value());
}

TEST_CASE("promptPreset.endo_signature_transparent_background_in_light_mode", "[prompt-presets]")
{
    auto const config = endo::promptPreset("endo-signature"sv, tui::ColorScheme::Light);
    CHECK(config.colorOverrides.transparentBackground);
    CHECK_FALSE(config.colorOverrides.background.has_value());
}
