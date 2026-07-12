// SPDX-License-Identifier: Apache-2.0
#include <tui/ImageLoader.hpp>
#include <tui/ImageProvider.hpp>
#include <tui/Sixel.hpp>

#include <algorithm>
#include <format>
#include <utility>

namespace tui
{

namespace
{
    /// @brief Number of colors in the quantized sixel palette.
    constexpr auto SixelPaletteSize = 256;

    /// @brief Fallback cell metrics used when the terminal reports no pixel size.
    constexpr auto FallbackCellWidthPx = 8;
    constexpr auto FallbackCellHeightPx = 16;

    /// @brief Divides @p value by @p divisor, rounding up. Both must be positive.
    [[nodiscard]] auto ceilDiv(int value, int divisor) noexcept -> int
    {
        return (value + divisor - 1) / divisor;
    }
} // namespace

auto isRemoteImageSource(std::string_view src) -> bool
{
    return src.find("://") != std::string_view::npos || src.starts_with("data:") || src.starts_with("//");
}

FilesystemImageProvider::FilesystemImageProvider(ImageRenderConfig config, SixelSupportFn sixelSupported):
    _config(std::move(config)), _sixelSupported(std::move(sixelSupported))
{
}

auto FilesystemImageProvider::supportsSixel() -> bool
{
    if (!_cachedSupport.has_value())
        _cachedSupport = _sixelSupported && _sixelSupported();
    return *_cachedSupport;
}

auto FilesystemImageProvider::prepare(std::string_view src, std::optional<int> requestedWidthPx)
    -> Result<PreparedImage>
{
    if (src.empty())
        return std::unexpected(std::string("empty image source"));

    if (isRemoteImageSource(src))
        return std::unexpected(std::format("remote image not supported: {}", src));

    auto const srcPath = std::filesystem::path(src);
    auto const resolved = srcPath.is_absolute() ? srcPath : _config.baseDir / srcPath;

    // Same gate the `cat` builtin applies to image files, so the supported
    // format set is declared exactly once.
    if (!isImageExtension(resolved.extension().string()))
        return std::unexpected(std::format("unsupported image format: {}", resolved.string()));

    auto const cellWidthPx = _config.cellWidthPx > 0 ? _config.cellWidthPx : FallbackCellWidthPx;
    auto const cellHeightPx = _config.cellHeightPx > 0 ? _config.cellHeightPx : FallbackCellHeightPx;
    auto const maxWidthPx = _config.maxColumns > 0 ? _config.maxColumns * cellWidthPx : 0;

    return loadImage(resolved.string())
        .and_then([&](OwnedImage image) -> Result<OwnedImage> {
            auto targetWidthPx = 0;
            if (requestedWidthPx.has_value())
                targetWidthPx = maxWidthPx > 0 ? std::min(*requestedWidthPx, maxWidthPx) : *requestedWidthPx;
            else if (maxWidthPx > 0 && image.width > maxWidthPx)
                targetWidthPx = maxWidthPx;

            // Never upscale, and skip the resize when nothing would change.
            if (targetWidthPx <= 0 || targetWidthPx >= image.width)
                return image;

            return resizeImage(image, targetWidthPx, 0);
        })
        .and_then([&](OwnedImage const& image) -> Result<PreparedImage> {
            auto const data =
                ImageData { .pixels = image.pixels, .width = image.width, .height = image.height };
            return encodeSixel(data, SixelPaletteSize).transform([&](std::string sixel) {
                return PreparedImage { .sixel = std::move(sixel),
                                       .cellWidth = ceilDiv(image.width, cellWidthPx),
                                       .cellHeight = ceilDiv(image.height, cellHeightPx) };
            });
        });
}

} // namespace tui
