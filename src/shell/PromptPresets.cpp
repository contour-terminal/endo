// SPDX-License-Identifier: Apache-2.0
#include "PromptPresets.hpp"

using tui::operator""_rgb;

namespace endo
{

PromptConfig promptPreset(std::string_view name)
{
    auto config = PromptConfig {};

    if (name == "minimal-arrow")
    {
        config.layout = PromptLayoutKind::SingleLine;
        config.separator = SeparatorStyle::None;
        config.indicator = "\xe2\x9d\xaf "; // ❯
        config.infoLineModules = {};
        config.rightPromptModules = {};
    }
    else if (name == "lambda-clean")
    {
        config.layout = PromptLayoutKind::SingleLine;
        config.separator = SeparatorStyle::None;
        config.indicator = "\xce\xbb "; // λ
        config.infoLineModules = { "path", "exit_status" };
        config.rightPromptModules = {};
    }
    else if (name == "opencode-bar")
    {
        config.layout = PromptLayoutKind::TwoLine;
        config.separator = SeparatorStyle::Bar;
        config.indicator = "> ";
        config.infoLineModules = { "path", "git" };
        config.rightPromptModules = {};
    }
    else if (name == "powerline")
    {
        config.layout = PromptLayoutKind::Powerline;
        config.separator = SeparatorStyle::Powerline;
        config.indicator = "\xe2\x9d\xaf "; // ❯
        config.infoLineModules = { "path", "git" };
        config.rightPromptModules = {};
    }
    else if (name == "transient")
    {
        config.layout = PromptLayoutKind::TwoLine;
        config.separator = SeparatorStyle::Rounded;
        config.indicator = "\xe2\x9d\xaf "; // ❯
        config.transient = TransientMode::Minimal;
        config.infoLineModules = { "path", "git" };
        config.rightPromptModules = {};
    }
    else if (name == "dashboard")
    {
        config.layout = PromptLayoutKind::TwoLine;
        config.separator = SeparatorStyle::None;
        config.indicator = "\xe2\x9d\xaf "; // ❯
        config.infoLineModules = { "path", "git" };
        config.rightPromptModules = { "duration", "battery", "clock" };
    }
    else if (name == "boxed-module")
    {
        config.layout = PromptLayoutKind::Boxed;
        config.separator = SeparatorStyle::Boxed;
        config.indicator = "\xe2\x9d\xaf\xe2\x9d\xaf "; // ❯❯
        config.infoLineModules = { "path", "git", "toolchain" };
        config.rightPromptModules = {};
    }
    else if (name == "gradient-glow")
    {
        config.layout = PromptLayoutKind::TwoLine;
        config.separator = SeparatorStyle::None;
        config.indicator = "\xe2\x9e\xa4\xe2\x9e\xa4\xe2\x9e\xa4 "; // ➤➤➤
        config.infoLineModules = { "path", "git" };
        config.rightPromptModules = {};
        config.useGradientPath = true;
        config.gradientStart = 0x5078FF_rgb; // Blue
        config.gradientEnd = 0x00DCC8_rgb;   // Teal
    }
    else if (name == "context-adaptive")
    {
        config.layout = PromptLayoutKind::SingleLine;
        config.separator = SeparatorStyle::None;
        config.indicator = "\xe2\x9d\xaf "; // ❯
        config.infoLineModules = { "hostname", "path", "git", "duration", "exit_status" };
        config.rightPromptModules = {};
    }
    else if (name == "endo-signature")
    {
        config.layout = PromptLayoutKind::TwoLine;
        config.separator = SeparatorStyle::Rounded;
        config.indicator = "|> ";
        config.infoLineModules = { "path", "git", "fsharp_mode", "structured_output" };
        config.rightPromptModules = { "duration", "exit_status" };
        config.useGradientPath = true;
        config.gradientStart = 0x5078FF_rgb; // Blue
        config.gradientEnd = 0x00DCC8_rgb;   // Teal
    }
    else
    {
        // Default: opencode-bar
        config.layout = PromptLayoutKind::TwoLine;
        config.separator = SeparatorStyle::Bar;
        config.indicator = "> ";
        config.infoLineModules = { "path", "git" };
    }

    return config;
}

std::vector<std::string_view> promptPresetNames()
{
    return {
        "minimal-arrow", "lambda-clean", "opencode-bar",  "powerline",        "transient",
        "dashboard",     "boxed-module", "gradient-glow", "context-adaptive", "endo-signature",
    };
}

} // namespace endo
