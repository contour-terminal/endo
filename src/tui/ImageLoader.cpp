// SPDX-License-Identifier: Apache-2.0
#include <tui/ImageLoader.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <string>

#include <stb_image.h>
#if defined(_MSC_VER) || defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wlanguage-extension-token"
#endif
#include <stb_image_resize2.h>
#if defined(_MSC_VER) || defined(__clang__)
    #pragma clang diagnostic pop
#endif

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

auto loadImageFromMemory(std::span<std::uint8_t const> data) -> Result<OwnedImage>
{
    auto width = 0;
    auto height = 0;
    auto channels = 0;

    auto* const pixels =
        stbi_load_from_memory(data.data(), static_cast<int>(data.size()), &width, &height, &channels, 4);
    if (!pixels)
        return std::unexpected(std::format("Failed to load image from memory: {}", stbi_failure_reason()));

    auto const pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    auto image = OwnedImage {
        .pixels = std::vector<std::uint8_t>(pixels, pixels + pixelCount),
        .width = width,
        .height = height,
    };
    stbi_image_free(pixels);
    return image;
}

auto detectImageMediaType(std::span<std::uint8_t const> data) -> std::string
{
    if (data.size() < 4)
        return {};

    // PNG: 89 50 4E 47 0D 0A 1A 0A
    if (data.size() >= 8 && data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47
        && data[4] == 0x0D && data[5] == 0x0A && data[6] == 0x1A && data[7] == 0x0A)
        return "image/png";

    // JPEG: FF D8 FF
    if (data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF)
        return "image/jpeg";

    // GIF: GIF87a or GIF89a
    if (data.size() >= 6 && data[0] == 'G' && data[1] == 'I' && data[2] == 'F' && data[3] == '8'
        && (data[4] == '7' || data[4] == '9') && data[5] == 'a')
        return "image/gif";

    // BMP: BM
    if (data[0] == 'B' && data[1] == 'M')
        return "image/bmp";

    // WebP: RIFF....WEBP
    if (data.size() >= 12 && data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F'
        && data[8] == 'W' && data[9] == 'E' && data[10] == 'B' && data[11] == 'P')
        return "image/webp";

    return {};
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

#if !defined(_WIN32)
namespace
{

    /// @brief Runs a command and captures its stdout as raw bytes.
    ///
    /// @param command Shell command to execute.
    /// @return Raw bytes from stdout, or empty vector on failure.
    auto runCommandAndCapture(char const* command) -> std::vector<std::uint8_t>
    {
        auto* pipe = popen(command, "r");
        if (!pipe)
            return {};

        auto result = std::vector<std::uint8_t>();
        auto buf = std::array<std::uint8_t, 4096>();
        while (auto const bytesRead = std::fread(buf.data(), 1, buf.size(), pipe))
            result.insert(result.end(), buf.data(), buf.data() + bytesRead);

        auto const status = pclose(pipe);
        if (status != 0)
            return {};

        return result;
    }

} // namespace
#endif

auto readClipboardImage() -> std::optional<ClipboardImage>
{
#ifdef _WIN32
    // Windows clipboard reading not yet implemented.
    return std::nullopt;
#else
    // Try each image format in order of preference.
    struct ClipboardQuery
    {
        char const* waylandCommand;
        char const* x11Command;
        char const* mediaType;
    };

    static constexpr auto Queries = std::array<ClipboardQuery, 3> { {
        { .waylandCommand = "wl-paste --no-newline --type image/png 2>/dev/null",
          .x11Command = "xclip -selection clipboard -target image/png -o 2>/dev/null",
          .mediaType = "image/png" },
        { .waylandCommand = "wl-paste --no-newline --type image/jpeg 2>/dev/null",
          .x11Command = "xclip -selection clipboard -target image/jpeg -o 2>/dev/null",
          .mediaType = "image/jpeg" },
        { .waylandCommand = "wl-paste --no-newline --type image/bmp 2>/dev/null",
          .x11Command = "xclip -selection clipboard -target image/bmp -o 2>/dev/null",
          .mediaType = "image/bmp" },
    } };

    auto const isWayland = std::getenv("WAYLAND_DISPLAY") != nullptr;

    for (auto const& query: Queries)
    {
        auto const* command = isWayland ? query.waylandCommand : query.x11Command;
        auto data = runCommandAndCapture(command);
        if (data.empty())
            continue;

        // Verify the data actually looks like the expected image format.
        auto const detected = detectImageMediaType(std::span<std::uint8_t const>(data));
        if (detected.empty())
            continue;

        return ClipboardImage {
            .data = std::move(data),
            .mediaType = detected,
        };
    }

    return std::nullopt;
#endif
}

} // namespace tui
