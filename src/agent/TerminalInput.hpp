// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace endo::agent
{

/// Reads a line from stdin with echo disabled (for API key entry).
/// @param prompt Optional prompt text to display before reading.
/// @return The entered text, or std::nullopt on EOF/error.
[[nodiscard]] auto readSecretLine(std::string_view prompt = {}) -> std::optional<std::string>;

/// Opens a URL in the system's default browser.
/// @param url The URL to open.
/// @return true if the browser was launched successfully.
[[nodiscard]] auto openBrowser(std::string_view url) -> bool;

} // namespace endo::agent
