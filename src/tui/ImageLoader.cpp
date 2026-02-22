// SPDX-License-Identifier: Apache-2.0
#include <tui/ImageLoader.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <format>
#include <string>

#include <stb_image.h>
#include <stb_image_resize2.h>

namespace tui
{

auto loadImage(std::string_view filePath) -> Result<OwnedImage>
{
    auto const pathStr = std::string(filePath);
    auto width = 0;
    auto height = 0;
    auto channels = 0;

    auto* const data = stbi_load(pathStr.c_str(), &width, &height, &channels, 4);
    if (!data)
        return std::unexpected(std::format("Failed to load image '{}': {}", filePath, stbi_failure_reason()));

    auto const pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    auto image = OwnedImage {
        .pixels = std::vector<std::uint8_t>(data, data + pixelCount),
        .width = width,
        .height = height,
    };
    stbi_image_free(data);
    return image;
}

auto resizeImage(OwnedImage const& image, int targetWidth, int targetHeight) -> Result<OwnedImage>
{
    if (image.width <= 0 || image.height <= 0)
        return std::unexpected(std::string("Source image has invalid dimensions"));

    if (targetWidth <= 0 && targetHeight <= 0)
        return std::unexpected(std::string("At least one target dimension must be positive"));

    // Auto-compute missing dimension preserving aspect ratio
    if (targetWidth <= 0)
        targetWidth = image.width * targetHeight / image.height;
    else if (targetHeight <= 0)
        targetHeight = image.height * targetWidth / image.width;

    if (targetWidth <= 0 || targetHeight <= 0)
        return std::unexpected(std::string("Computed target dimensions are invalid"));

    auto const outputSize =
        static_cast<std::size_t>(targetWidth) * static_cast<std::size_t>(targetHeight) * 4;
    auto result = OwnedImage {
        .pixels = std::vector<std::uint8_t>(outputSize),
        .width = targetWidth,
        .height = targetHeight,
    };

    auto const* resized = stbir_resize_uint8_srgb(image.pixels.data(),
                                                  image.width,
                                                  image.height,
                                                  image.width * 4,
                                                  result.pixels.data(),
                                                  targetWidth,
                                                  targetHeight,
                                                  targetWidth * 4,
                                                  STBIR_RGBA);
    if (!resized)
        return std::unexpected(std::string("Failed to resize image"));

    return result;
}

auto isImageExtension(std::string_view ext) -> bool
{
    if (ext.empty())
        return false;

    // Normalize to lowercase for comparison
    auto lower = std::string(ext);
    std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return std::tolower(c); });

    return lower == ".png" || lower == ".jpg" || lower == ".jpeg" || lower == ".gif" || lower == ".bmp"
           || lower == ".tga" || lower == ".psd" || lower == ".hdr" || lower == ".pnm" || lower == ".pgm"
           || lower == ".ppm";
}

} // namespace tui
