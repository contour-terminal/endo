// SPDX-License-Identifier: Apache-2.0
#include <tui/Sixel.hpp>

#include <algorithm>
#include <format>
#include <limits>
#include <string>
#include <vector>

namespace tui
{

namespace
{
    struct RgbPixel
    {
        std::uint8_t r = 0;
        std::uint8_t g = 0;
        std::uint8_t b = 0;
    };

    struct ColorBucket
    {
        std::vector<RgbPixel> pixels;
    };

    /// @brief Finds the channel (0=R, 1=G, 2=B) with the widest range in the bucket.
    auto widestChannel(ColorBucket const& bucket) -> int
    {
        auto minR = std::uint8_t { 255 };
        auto maxR = std::uint8_t { 0 };
        auto minG = std::uint8_t { 255 };
        auto maxG = std::uint8_t { 0 };
        auto minB = std::uint8_t { 255 };
        auto maxB = std::uint8_t { 0 };
        for (auto const& p: bucket.pixels)
        {
            minR = std::min(minR, p.r);
            maxR = std::max(maxR, p.r);
            minG = std::min(minG, p.g);
            maxG = std::max(maxG, p.g);
            minB = std::min(minB, p.b);
            maxB = std::max(maxB, p.b);
        }
        auto const rangeR = maxR - minR;
        auto const rangeG = maxG - minG;
        auto const rangeB = maxB - minB;
        if (rangeR >= rangeG && rangeR >= rangeB)
            return 0;
        if (rangeG >= rangeR && rangeG >= rangeB)
            return 1;
        return 2;
    }

    /// @brief Computes the average color of a bucket.
    auto averageColor(ColorBucket const& bucket) -> RgbPixel
    {
        if (bucket.pixels.empty())
            return {};
        auto sumR = std::uint64_t { 0 };
        auto sumG = std::uint64_t { 0 };
        auto sumB = std::uint64_t { 0 };
        for (auto const& p: bucket.pixels)
        {
            sumR += p.r;
            sumG += p.g;
            sumB += p.b;
        }
        auto const n = static_cast<std::uint64_t>(bucket.pixels.size());
        auto result = RgbPixel {};
        result.r = static_cast<std::uint8_t>(sumR / n);
        result.g = static_cast<std::uint8_t>(sumG / n);
        result.b = static_cast<std::uint8_t>(sumB / n);
        return result;
    }

    /// @brief Median-cut color quantization.
    /// @param pixels The input pixels.
    /// @param maxColors Target palette size.
    /// @return Quantized palette.
    auto medianCut(std::vector<RgbPixel> const& pixels, int maxColors) -> std::vector<RgbPixel>
    {
        auto buckets = std::vector<ColorBucket> {};
        buckets.push_back(ColorBucket { .pixels = pixels });

        while (static_cast<int>(buckets.size()) < maxColors)
        {
            // Find the bucket with the most pixels
            auto largestIdx = std::size_t { 0 };
            auto largestSize = std::size_t { 0 };
            for (auto i = std::size_t { 0 }; i < buckets.size(); ++i)
            {
                if (buckets[i].pixels.size() > largestSize)
                {
                    largestSize = buckets[i].pixels.size();
                    largestIdx = i;
                }
            }

            if (largestSize <= 1)
                break;

            auto& bucket = buckets[largestIdx];
            auto const channel = widestChannel(bucket);

            // Sort by the widest channel
            std::ranges::sort(bucket.pixels, [channel](auto const& a, auto const& b) {
                switch (channel)
                {
                    case 0: return a.r < b.r;
                    case 1: return a.g < b.g;
                    default: return a.b < b.b;
                }
            });

            // Split at the median
            auto const mid = bucket.pixels.size() / 2;
            auto newBucket = ColorBucket {};
            newBucket.pixels.assign(bucket.pixels.begin() + static_cast<std::ptrdiff_t>(mid),
                                    bucket.pixels.end());
            bucket.pixels.resize(mid);
            buckets.push_back(std::move(newBucket));
        }

        auto palette = std::vector<RgbPixel> {};
        palette.reserve(buckets.size());
        for (auto const& bucket: buckets)
            palette.push_back(averageColor(bucket));
        return palette;
    }

    /// @brief Finds the closest palette color to the given pixel.
    auto closestColor(RgbPixel const& pixel, std::vector<RgbPixel> const& palette) -> int
    {
        auto bestIdx = 0;
        auto bestDist = std::numeric_limits<int>::max();
        for (auto i = 0; i < static_cast<int>(palette.size()); ++i)
        {
            auto const& c = palette[static_cast<std::size_t>(i)];
            auto const dr = static_cast<int>(pixel.r) - static_cast<int>(c.r);
            auto const dg = static_cast<int>(pixel.g) - static_cast<int>(c.g);
            auto const db = static_cast<int>(pixel.b) - static_cast<int>(c.b);
            auto const dist = dr * dr + dg * dg + db * db;
            if (dist < bestDist)
            {
                bestDist = dist;
                bestIdx = i;
            }
        }
        return bestIdx;
    }
} // namespace

auto encodeSixel(ImageData const& image, int maxColors) -> Result<std::string>
{
    if (image.width <= 0 || image.height <= 0)
        return std::unexpected(std::string("Image dimensions must be positive"));

    auto const pixelCount = static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height);
    if (image.pixels.size() < pixelCount * 4)
        return std::unexpected(std::string("Pixel data too small for given dimensions"));

    maxColors = std::clamp(maxColors, 1, 256);

    // Extract RGB pixels (ignore alpha)
    auto pixels = std::vector<RgbPixel>(pixelCount);
    for (auto i = std::size_t { 0 }; i < pixelCount; ++i)
    {
        auto pixel = RgbPixel {};
        pixel.r = image.pixels[i * 4];
        pixel.g = image.pixels[i * 4 + 1];
        pixel.b = image.pixels[i * 4 + 2];
        pixels[i] = pixel;
    }

    // Quantize colors
    auto const palette = medianCut(pixels, maxColors);

    // Map each pixel to a palette index
    auto indexed = std::vector<int>(pixelCount);
    for (auto i = std::size_t { 0 }; i < pixelCount; ++i)
        indexed[i] = closestColor(pixels[i], palette);

    // Build sixel output
    auto result = std::string {};

    // Define palette: #idx;2;r%;g%;b% (percentage 0-100)
    for (auto i = 0; i < static_cast<int>(palette.size()); ++i)
    {
        auto const& c = palette[static_cast<std::size_t>(i)];
        result += std::format("#{};2;{};{};{}", i, c.r * 100 / 255, c.g * 100 / 255, c.b * 100 / 255);
    }

    // Encode sixel data: process in bands of 6 rows
    auto const width = image.width;
    auto const height = image.height;
    for (auto bandY = 0; bandY < height; bandY += 6)
    {
        for (auto colorIdx = 0; colorIdx < static_cast<int>(palette.size()); ++colorIdx)
        {
            auto hasPixels = false;
            auto row = std::string {};
            for (auto x = 0; x < width; ++x)
            {
                auto sixelByte = std::uint8_t { 0 };
                for (auto bit = 0; bit < 6; ++bit)
                {
                    auto const y = bandY + bit;
                    if (y < height)
                    {
                        auto const pixelIdx = static_cast<std::size_t>(y) * static_cast<std::size_t>(width)
                                              + static_cast<std::size_t>(x);
                        if (indexed[pixelIdx] == colorIdx)
                        {
                            sixelByte |= static_cast<std::uint8_t>(1 << bit);
                            hasPixels = true;
                        }
                    }
                }
                row += static_cast<char>(sixelByte + 63);
            }
            if (hasPixels)
            {
                result += std::format("#{}", colorIdx);
                result += row;
                result += '$'; // Carriage return (go to beginning of same sixel band)
            }
        }
        result += '-'; // New line (advance to next sixel band)
    }

    return result;
}

} // namespace tui
