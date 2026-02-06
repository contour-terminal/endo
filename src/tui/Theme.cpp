// SPDX-License-Identifier: Apache-2.0
#include <tui/Theme.hpp>

namespace tui
{

auto darkTheme() -> Theme
{
    auto theme = Theme {};

    // Color palette - Dark theme
    theme.colors = ColorPalette {
        .primary = RgbColor { .r = 130, .g = 180, .b = 255 },       // Light blue
        .secondary = RgbColor { .r = 180, .g = 130, .b = 255 },     // Light purple
        .accent = RgbColor { .r = 255, .g = 180, .b = 130 },        // Light orange
        .background = RgbColor { .r = 30, .g = 30, .b = 36 },       // Dark gray
        .surface = RgbColor { .r = 40, .g = 42, .b = 54 },          // Slightly lighter
        .overlay = RgbColor { .r = 50, .g = 52, .b = 64 },          // Dialog background
        .text = RgbColor { .r = 248, .g = 248, .b = 242 },          // Off-white
        .textMuted = RgbColor { .r = 140, .g = 140, .b = 150 },     // Gray
        .textInverse = RgbColor { .r = 30, .g = 30, .b = 36 },      // Dark for light bg
        .success = RgbColor { .r = 80, .g = 250, .b = 123 },        // Green
        .warning = RgbColor { .r = 255, .g = 184, .b = 108 },       // Orange
        .error = RgbColor { .r = 255, .g = 85, .b = 85 },           // Red
        .info = RgbColor { .r = 139, .g = 233, .b = 253 },          // Cyan
        .border = RgbColor { .r = 68, .g = 71, .b = 90 },           // Subtle border
        .borderFocused = RgbColor { .r = 130, .g = 180, .b = 255 }, // Primary accent
    };

    // Text styles
    theme.textNormal.fg = theme.colors.text;
    theme.textMuted.fg = theme.colors.textMuted;
    theme.textMuted.dim = true;
    theme.textBold.fg = theme.colors.text;
    theme.textBold.bold = true;
    theme.textAccent.fg = theme.colors.primary;
    theme.textAccent.bold = true;

    // Button styles
    theme.buttonNormal.fg = theme.colors.text;
    theme.buttonFocused.fg = theme.colors.primary;
    theme.buttonFocused.bold = true;
    theme.buttonFocused.inverse = true;
    theme.buttonDisabled.fg = theme.colors.textMuted;
    theme.buttonDisabled.dim = true;

    // List styles
    theme.listItem.fg = theme.colors.text;
    theme.listItemSelected.fg = theme.colors.primary;
    theme.listItemSelected.bold = true;
    theme.listItemDisabled.fg = theme.colors.textMuted;
    theme.listItemDisabled.dim = true;

    // Input styles
    theme.inputNormal.fg = theme.colors.text;
    theme.inputFocused.fg = theme.colors.text;
    theme.inputPlaceholder.fg = theme.colors.textMuted;
    theme.inputPlaceholder.dim = true;

    // Dialog styles
    theme.dialogBorder.fg = theme.colors.border;
    theme.dialogTitle.fg = theme.colors.primary;
    theme.dialogTitle.bold = true;
    theme.dialogBackground.bg = theme.colors.overlay;

    // Status bar styles
    theme.statusBackground.bg = theme.colors.surface;
    theme.statusKey.fg = theme.colors.primary;
    theme.statusKey.bold = true;
    theme.statusAction.fg = theme.colors.textMuted;

    // Semantic styles
    theme.success.fg = theme.colors.success;
    theme.warning.fg = theme.colors.warning;
    theme.error.fg = theme.colors.error;
    theme.info.fg = theme.colors.info;

    // Completion styles
    theme.ghostText.dim = true;                         // Default ghost text is dim (SGR 2)
    theme.completionItem.fg = theme.colors.text;        // Normal menu item
    theme.completionSelected.fg = theme.colors.primary; // Selected item
    theme.completionSelected.inverse = true;            // Inverse for visibility
    theme.completionDesc.fg = theme.colors.textMuted;   // Description text
    theme.completionMatch.fg = theme.colors.info;       // Highlighted matched chars (cyan)
    theme.completionMatch.bold = true;

    theme.borderStyle = BorderStyle::Rounded;

    return theme;
}

auto lightTheme() -> Theme
{
    auto theme = Theme {};

    // Color palette - Light theme
    theme.colors = ColorPalette {
        .primary = RgbColor { .r = 0, .g = 100, .b = 200 },       // Blue
        .secondary = RgbColor { .r = 100, .g = 0, .b = 180 },     // Purple
        .accent = RgbColor { .r = 200, .g = 100, .b = 0 },        // Orange
        .background = RgbColor { .r = 250, .g = 250, .b = 250 },  // Off-white
        .surface = RgbColor { .r = 240, .g = 240, .b = 245 },     // Light gray
        .overlay = RgbColor { .r = 255, .g = 255, .b = 255 },     // White
        .text = RgbColor { .r = 40, .g = 42, .b = 54 },           // Dark gray
        .textMuted = RgbColor { .r = 120, .g = 120, .b = 130 },   // Medium gray
        .textInverse = RgbColor { .r = 255, .g = 255, .b = 255 }, // White
        .success = RgbColor { .r = 0, .g = 150, .b = 80 },        // Green
        .warning = RgbColor { .r = 200, .g = 130, .b = 0 },       // Orange
        .error = RgbColor { .r = 200, .g = 50, .b = 50 },         // Red
        .info = RgbColor { .r = 0, .g = 130, .b = 180 },          // Cyan
        .border = RgbColor { .r = 200, .g = 200, .b = 210 },      // Light border
        .borderFocused = RgbColor { .r = 0, .g = 100, .b = 200 }, // Primary
    };

    // Apply same style patterns as dark theme
    theme.textNormal.fg = theme.colors.text;
    theme.textMuted.fg = theme.colors.textMuted;
    theme.textBold.fg = theme.colors.text;
    theme.textBold.bold = true;
    theme.textAccent.fg = theme.colors.primary;
    theme.textAccent.bold = true;

    theme.buttonNormal.fg = theme.colors.text;
    theme.buttonFocused.fg = theme.colors.primary;
    theme.buttonFocused.bold = true;
    theme.buttonDisabled.fg = theme.colors.textMuted;
    theme.buttonDisabled.dim = true;

    theme.listItem.fg = theme.colors.text;
    theme.listItemSelected.fg = theme.colors.primary;
    theme.listItemSelected.bold = true;
    theme.listItemDisabled.fg = theme.colors.textMuted;
    theme.listItemDisabled.dim = true;

    theme.inputNormal.fg = theme.colors.text;
    theme.inputFocused.fg = theme.colors.text;
    theme.inputPlaceholder.fg = theme.colors.textMuted;

    theme.dialogBorder.fg = theme.colors.border;
    theme.dialogTitle.fg = theme.colors.primary;
    theme.dialogTitle.bold = true;
    theme.dialogBackground.bg = theme.colors.overlay;

    theme.statusBackground.bg = theme.colors.surface;
    theme.statusKey.fg = theme.colors.primary;
    theme.statusKey.bold = true;
    theme.statusAction.fg = theme.colors.textMuted;

    theme.success.fg = theme.colors.success;
    theme.warning.fg = theme.colors.warning;
    theme.error.fg = theme.colors.error;
    theme.info.fg = theme.colors.info;

    // Completion styles
    theme.ghostText.dim = true;                         // Default ghost text is dim (SGR 2)
    theme.completionItem.fg = theme.colors.text;        // Normal menu item
    theme.completionSelected.fg = theme.colors.primary; // Selected item
    theme.completionSelected.inverse = true;            // Inverse for visibility
    theme.completionDesc.fg = theme.colors.textMuted;   // Description text
    theme.completionMatch.fg = theme.colors.info;       // Highlighted matched chars (cyan)
    theme.completionMatch.bold = true;

    theme.borderStyle = BorderStyle::Rounded;

    return theme;
}

auto monoTheme() -> Theme
{
    auto theme = Theme {};

    // Monochrome palette using 256-color indexes
    theme.colors = ColorPalette {
        .primary = RgbColor { .r = 255, .g = 255, .b = 255 },
        .secondary = RgbColor { .r = 200, .g = 200, .b = 200 },
        .accent = RgbColor { .r = 255, .g = 255, .b = 255 },
        .background = RgbColor { .r = 0, .g = 0, .b = 0 },
        .surface = RgbColor { .r = 30, .g = 30, .b = 30 },
        .overlay = RgbColor { .r = 40, .g = 40, .b = 40 },
        .text = RgbColor { .r = 255, .g = 255, .b = 255 },
        .textMuted = RgbColor { .r = 128, .g = 128, .b = 128 },
        .textInverse = RgbColor { .r = 0, .g = 0, .b = 0 },
        .success = RgbColor { .r = 255, .g = 255, .b = 255 },
        .warning = RgbColor { .r = 255, .g = 255, .b = 255 },
        .error = RgbColor { .r = 255, .g = 255, .b = 255 },
        .info = RgbColor { .r = 255, .g = 255, .b = 255 },
        .border = RgbColor { .r = 100, .g = 100, .b = 100 },
        .borderFocused = RgbColor { .r = 255, .g = 255, .b = 255 },
    };

    // Simple monochrome styles
    theme.textNormal.fg = theme.colors.text;
    theme.textMuted.dim = true;
    theme.textBold.bold = true;
    theme.textAccent.bold = true;
    theme.textAccent.underline = true;

    theme.buttonNormal = {};
    theme.buttonFocused.inverse = true;
    theme.buttonDisabled.dim = true;

    theme.listItem = {};
    theme.listItemSelected.inverse = true;
    theme.listItemDisabled.dim = true;

    theme.inputNormal = {};
    theme.inputFocused = {};
    theme.inputPlaceholder.dim = true;

    theme.dialogBorder = {};
    theme.dialogTitle.bold = true;
    theme.dialogBackground = {};

    theme.statusBackground.inverse = true;
    theme.statusKey.bold = true;
    theme.statusAction = {};

    theme.success.bold = true;
    theme.warning.bold = true;
    theme.warning.underline = true;
    theme.error.bold = true;
    theme.error.inverse = true;
    theme.info.bold = true;

    // Completion styles
    theme.ghostText.dim = true;              // Default ghost text is dim
    theme.completionItem = {};               // Normal
    theme.completionSelected.inverse = true; // Selected
    theme.completionDesc.dim = true;         // Description
    theme.completionMatch.bold = true;       // Highlighted matched chars (bold for mono)
    theme.completionMatch.underline = true;

    theme.borderStyle = BorderStyle::Single;

    return theme;
}

// =============================================================================
// ThemeManager
// =============================================================================

ThemeManager::ThemeManager(): _current(darkTheme())
{
}

auto ThemeManager::instance() -> ThemeManager&
{
    static auto manager = ThemeManager {};
    return manager;
}

auto ThemeManager::current() const noexcept -> Theme const&
{
    return _current;
}

void ThemeManager::setCurrent(Theme theme)
{
    _current = std::move(theme);
}

void ThemeManager::reset()
{
    _current = darkTheme();
}

auto currentTheme() -> Theme const&
{
    return ThemeManager::instance().current();
}

} // namespace tui
