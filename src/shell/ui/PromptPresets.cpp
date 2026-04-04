// SPDX-License-Identifier: Apache-2.0
#include "PromptPresets.hpp"

using tui::operator""_rgb;

using namespace std::string_view_literals;

namespace endo
{

static std::vector<PromptConfig> presets = {
    {
        .name = "minimal-arrow"sv,
        .layout = PromptLayoutKind::SingleLine,
        .separator = SeparatorStyle::None,
        .indicator = "\xe2\x9d\xaf ", // ❯
        .infoLineModules = {},
        .rightPromptModules = {},
        .colorOverrides = { .transparentBackground = true },
    },
    PromptConfig {
        .name = "lambda-clean"sv,
        .layout = PromptLayoutKind::SingleLine,
        .separator = SeparatorStyle::None,
        .indicator = "\xce\xbb ", // λ
        .infoLineModules = { "path", "exit_status" },
        .rightPromptModules = {},
    },
    PromptConfig {
        .name = "opencode-bar"sv,
        .layout = PromptLayoutKind::TwoLine,
        .separator = SeparatorStyle::Bar,
        .indicator = "> ",
        .infoLineModules = { "path", "git" },
        .rightPromptModules = {},
    },
    PromptConfig {
        .name = "powerline"sv,
        .layout = PromptLayoutKind::Powerline,
        .separator = SeparatorStyle::Powerline,
        .indicator = "\xe2\x9d\xaf ", // ❯
        .infoLineModules = { "path", "git" },
        .rightPromptModules = {},
    },
    PromptConfig {
        .name = "transient"sv,
        .layout = PromptLayoutKind::TwoLine,
        .separator = SeparatorStyle::Rounded,
        .transient = TransientMode::Arrow,
        .indicator = "\xe2\x9d\xaf ", // ❯
        .infoLineModules = { "path", "git" },
        .rightPromptModules = {},
    },
    PromptConfig {
        .name = "dashboard"sv,
        .layout = PromptLayoutKind::TwoLine,
        .separator = SeparatorStyle::None,
        .indicator = "\xe2\x9d\xaf ", // ❯
        .infoLineModules = { "path", "git" },
        .rightPromptModules = { "duration", "shell_level", "battery", "clock" },
    },
    PromptConfig {
        .name = "boxed-module"sv,
        .layout = PromptLayoutKind::Boxed,
        .separator = SeparatorStyle::Boxed,
        .indicator = "\xe2\x9d\xaf\xe2\x9d\xaf ", // ❯❯
        .infoLineModules = { "path", "git", "toolchain" },
        .rightPromptModules = {},
    },
    PromptConfig {
        .name = "gradient-glow"sv,
        .layout = PromptLayoutKind::TwoLine,
        .separator = SeparatorStyle::None,
        .indicator = "\xe2\x9e\xa4\xe2\x9e\xa4\xe2\x9e\xa4 ", // ➤➤➤
        .infoLineModules = { "path", "git" },
        .rightPromptModules = {},
        .colorOverrides = { .path = ColorSpec { { 0x5078FF_rgb, 0x00DCC8_rgb } } }, // Blue → Teal gradient
    },
    PromptConfig {
        .name = "context-adaptive"sv,
        .layout = PromptLayoutKind::SingleLine,
        .separator = SeparatorStyle::None,
        .indicator = "\xe2\x9d\xaf ", // ❯
        .infoLineModules = { "hostname", "path", "git", "duration", "exit_status" },
        .rightPromptModules = {},
    },
    PromptConfig {
        .name = "endo-signature"sv,
        .layout = PromptLayoutKind::TwoLine,
        .separator = SeparatorStyle::Rounded,
        .indicator = "|> ",
        .infoLineModules = { "path", "git", "fsharp_mode", "structured_output" },
        .rightPromptModules = { "duration", "exit_status", "shell_level", "battery", "clock" },
        .auroraBackground = {
            0x252545_rgb, // deep indigo
            0x1E3840_rgb, // dark teal
            0x1E3828_rgb, // dark emerald
            0x352040_rgb, // dark purple
            0x252545_rgb, // deep indigo (wrap)
        },
        .enableSixelFade = false,
        .colorOverrides = { .path = ColorSpec { { 0x5078FF_rgb, 0x00DCC8_rgb } } }, // Blue → Teal gradient
    },
};

/// @brief Overrides gradient and aurora colors for light-mode terminals.
static void applyLightOverrides(PromptConfig& config)
{
    if (config.name == "endo-signature"sv)
    {
        config.colorOverrides.path = ColorSpec { { 0x3060D8_rgb, 0x009688_rgb } }; // Darker blue → teal
        config.auroraBackground = {
            0xD8DCF0_rgb, // soft lavender
            0xCCE8E8_rgb, // soft teal
            0xCCE8D4_rgb, // soft mint
            0xE4D0EC_rgb, // soft mauve
            0xD8DCF0_rgb, // soft lavender (wrap)
        };
    }
    else if (config.name == "gradient-glow"sv)
    {
        config.colorOverrides.path = ColorSpec { { 0x3060D8_rgb, 0x009688_rgb } }; // Darker blue → teal
    }
}

PromptConfig promptPreset(std::string_view name, tui::ColorScheme scheme)
{
    for (const auto& preset: presets)
        if (preset.name == name)
        {
            auto config = preset;
            if (scheme == tui::ColorScheme::Light)
                applyLightOverrides(config);
            return config;
        }

    return PromptConfig {};
}

std::vector<std::string_view> promptPresetNames()
{
    std::vector<std::string_view> names;
    names.reserve(presets.size());
    for (const auto& preset: presets)
        names.push_back(preset.name);
    return names;
}

} // namespace endo
