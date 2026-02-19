// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <string_view>

namespace endo::agent
{

/// @brief URL-encodes a string for use in query parameters.
/// @param input The string to encode.
/// @return The percent-encoded string.
[[nodiscard]] auto urlEncode(std::string_view input) -> std::string;

/// @brief Strips HTML tags from a string, returning only text content.
/// @param html The HTML string to strip.
/// @return The text content without HTML tags.
[[nodiscard]] auto stripHtmlTags(std::string_view html) -> std::string;

/// @brief Decodes common HTML entities (&amp;, &lt;, &gt;, &quot;, etc.).
/// @param text The string containing HTML entities.
/// @return The decoded string.
[[nodiscard]] auto decodeHtmlEntities(std::string_view text) -> std::string;

} // namespace endo::agent
