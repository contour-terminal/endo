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

tui::RgbColor multiStopGradient(std::span<tui::RgbColor const> stops, float t) noexcept
{
    if (stops.empty())
        return { 0, 0, 0 };

    if (stops.size() == 1)
        return stops[0];

    // Clamp t to [0, 1]
    t = std::clamp(t, 0.0f, 1.0f);

    auto const segments = static_cast<float>(stops.size() - 1);
    auto const scaled = t * segments;
    auto const idx = static_cast<std::size_t>(scaled);
    auto const frac = scaled - static_cast<float>(idx);

    // At t=1.0, idx might equal stops.size()-1, return last stop
    if (idx >= stops.size() - 1)
        return stops.back();

    return tui::lerpColor(stops[idx], stops[idx + 1], frac);
}

namespace
{

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
        style.fg = tui::lerpColor(start, end, t);
        segments.push_back(PromptSegment { .text = std::string(text.substr(spans[i].offset, spans[i].length)),
                                           .style = style });
    }

    return segments;
}

} // namespace endo
