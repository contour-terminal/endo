// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tui/Error.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

namespace tui
{

/// @brief Owned RGBA image data.
struct OwnedImage
{
    std::vector<std::uint8_t> pixels; ///< RGBA pixel data (4 bytes per pixel).
    int width = 0;                    ///< Image width in pixels.
    int height = 0;                   ///< Image height in pixels.
};

/// @brief Loads an image file from disk into RGBA pixel data.
///
/// Supports PNG, JPEG, GIF, BMP, TGA, PSD, HDR, PNM, PGM, PPM formats.
///
/// @param filePath Path to the image file.
/// @return Owned image data on success, or an error string.
[[nodiscard]] auto loadImage(std::string_view filePath) -> Result<OwnedImage>;

/// @brief Resizes an image to target dimensions, preserving aspect ratio.
///
/// If one target dimension is 0, it is computed from the aspect ratio of the source image.
/// Both dimensions must not be 0 simultaneously.
///
/// @param image Source image to resize.
/// @param targetWidth Desired width (0 to auto-compute from height).
/// @param targetHeight Desired height (0 to auto-compute from width).
/// @return Resized image on success, or an error string.
[[nodiscard]] auto resizeImage(OwnedImage const& image, int targetWidth, int targetHeight)
    -> Result<OwnedImage>;

/// @brief Checks if a file extension corresponds to a supported image format.
///
/// Supported: .png .jpg .jpeg .gif .bmp .tga .psd .hdr .pnm .pgm .ppm
///
/// @param ext File extension including the dot (e.g., ".png").
/// @return true if the extension is a recognized image format.
[[nodiscard]] auto isImageExtension(std::string_view ext) -> bool;

} // namespace tui
