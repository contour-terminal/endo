// SPDX-License-Identifier: Apache-2.0
#include "Gradient.hpp"

#include <algorithm>

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wold-style-cast"
#endif
#include <libunicode/utf8_grapheme_segmenter.h>
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

namespace endo
{

namespace
{

    /// @brief Linearly interpolates between two uint8_t values.
    [[nodiscard]] constexpr auto lerp(std::uint8_t a, std::uint8_t b, float t) noexcept -> std::uint8_t
    {
        return static_cast<std::uint8_t>(std::clamp(
            static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * t, 0.0f, 255.0f));
    }

    /// @brief Interpolates between two RGB colors.
    [[nodiscard]] constexpr auto lerpColor(tui::RgbColor a, tui::RgbColor b, float t) noexcept
        -> tui::RgbColor
    {
        return { .r = lerp(a.r, b.r, t), .g = lerp(a.g, b.g, t), .b = lerp(a.b, b.b, t) };
    }

    /// @brief Returns the number of UTF-8 bytes for a codepoint.
    [[nodiscard]] constexpr auto utf8ByteLength(char32_t cp) noexcept -> std::size_t
    {
        if (cp <= 0x7F)
            return 1;
        if (cp <= 0x7FF)
            return 2;
        if (cp <= 0xFFFF)
            return 3;
        return 4;
    }

} // namespace

PromptSegments gradient(tui::RgbColor start, tui::RgbColor end, std::string_view text)
{
    if (text.empty())
        return {};

    // Collect grapheme clusters as byte spans in the original UTF-8 text
    struct ClusterSpan
    {
        std::size_t offset;
        std::size_t length;
    };

    auto spans = std::vector<ClusterSpan> {};

    auto segmenter = unicode::utf8_grapheme_segmenter(text);
    std::size_t bytePos = 0;
    for (auto const& cluster: segmenter)
    {
        // Compute byte length of the cluster from its codepoints
        std::size_t clusterBytes = 0;
        for (auto const cp: cluster)
            clusterBytes += utf8ByteLength(cp);
        spans.push_back({ bytePos, clusterBytes });
        bytePos += clusterBytes;
    }

    if (spans.empty())
        return {};

    auto const count = spans.size();
    auto segments = PromptSegments {};
    segments.reserve(count);

    for (std::size_t i = 0; i < count; ++i)
    {
        auto const t = (count == 1) ? 0.0f : static_cast<float>(i) / static_cast<float>(count - 1);
        auto style = tui::Style {};
        style.fg = lerpColor(start, end, t);
        segments.push_back(PromptSegment { .text = std::string(text.substr(spans[i].offset, spans[i].length)),
                                           .style = style });
    }

    return segments;
}

} // namespace endo
