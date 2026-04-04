// SPDX-License-Identifier: Apache-2.0
#include "PromptColorResolver.hpp"

#include <charconv>
#include <format>

namespace endo
{

namespace
{

    /// @brief Wraps a single RgbColor into a ColorSpec.
    [[nodiscard]] ColorSpec wrapSolid(tui::RgbColor color)
    {
        return ColorSpec { .colors = { color } };
    }

    /// @brief Resolves an optional override against a theme default.
    [[nodiscard]] ColorSpec resolveField(std::optional<ColorSpec> const& override, tui::RgbColor themeDefault)
    {
        return override.value_or(wrapSolid(themeDefault));
    }

    /// @brief Parses a single hex color string (e.g., "#RRGGBB" or "0xRRGGBB").
    [[nodiscard]] std::optional<tui::RgbColor> parseHexColor(std::string_view str)
    {
        // Strip prefix
        if (str.starts_with('#'))
            str.remove_prefix(1);
        else if (str.starts_with("0x") || str.starts_with("0X"))
            str.remove_prefix(2);
        else
            return std::nullopt;

        if (str.size() != 6)
            return std::nullopt;

        auto value = std::uint32_t {};
        auto const [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value, 16);
        if (ec != std::errc {} || ptr != str.data() + str.size())
            return std::nullopt;

        return tui::RgbColor { .r = static_cast<std::uint8_t>((value >> 16) & 0xFF),
                               .g = static_cast<std::uint8_t>((value >> 8) & 0xFF),
                               .b = static_cast<std::uint8_t>(value & 0xFF) };
    }

} // namespace

ResolvedPromptColors resolvePromptColors(PromptColorOverrides const& overrides,
                                         tui::Theme::PromptColorPalette const& themeColors)
{
    auto resolved = ResolvedPromptColors {};

    resolved.path = resolveField(overrides.path, themeColors.path);
    resolved.gitClean = resolveField(overrides.gitClean, themeColors.gitClean);
    resolved.gitDirty = resolveField(overrides.gitDirty, themeColors.gitDirty);
    resolved.gitStaged = resolveField(overrides.gitStaged, themeColors.gitStaged);
    resolved.indicator = resolveField(overrides.indicator, themeColors.indicator);
    resolved.indicatorError = resolveField(overrides.indicatorError, themeColors.indicatorError);
    resolved.exitCode = resolveField(overrides.exitCode, themeColors.exitCode);
    resolved.duration = resolveField(overrides.duration, themeColors.duration);
    resolved.hostname = resolveField(overrides.hostname, themeColors.hostname);
    resolved.separator = resolveField(overrides.separator, themeColors.separator);
    resolved.badge = resolveField(overrides.badge, themeColors.badge);
    resolved.badgeText = resolveField(overrides.badgeText, themeColors.badgeText);
    resolved.clock = resolveField(overrides.clock, themeColors.clock);

    // Background: transparent flag takes priority, then override, then theme default
    if (overrides.transparentBackground)
        resolved.background = tui::Color {}; // std::monostate — no background emitted
    else if (overrides.background)
        resolved.background = overrides.background->solid();
    else
        resolved.background = themeColors.background;

    return resolved;
}

std::optional<ColorSpec> parseColorSpec(std::string_view str)
{
    if (str.empty())
        return std::nullopt;

    auto colors = std::vector<tui::RgbColor> {};

    // Split on colons for gradient stops
    while (!str.empty())
    {
        auto const colonPos = str.find(':');
        auto const part = str.substr(0, colonPos);

        auto const color = parseHexColor(part);
        if (!color)
            return std::nullopt;

        colors.push_back(*color);

        if (colonPos == std::string_view::npos)
            break;
        str.remove_prefix(colonPos + 1);
    }

    if (colors.empty())
        return std::nullopt;

    return ColorSpec { .colors = std::move(colors) };
}

std::string formatColorSpec(ColorSpec const& spec)
{
    if (spec.colors.empty())
        return {};

    auto result = std::string {};
    auto first = true;
    for (auto const& c: spec.colors)
    {
        if (!first)
            result += ':';
        first = false;
        result += std::format("#{:02X}{:02X}{:02X}", c.r, c.g, c.b);
    }
    return result;
}

} // namespace endo
