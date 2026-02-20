// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace endo::agent
{

/// @brief Presents a single-select question using the TUI QuestionComponent.
///
/// Creates and manages Terminal + Screen (Inline viewport) internally.
/// @param question The question text to display.
/// @param options The selectable options.
/// @return The selected option index (0-based), or std::nullopt on cancel/Esc.
[[nodiscard]] auto askSingleSelect(std::string_view question, std::span<std::string_view const> options)
    -> std::optional<std::size_t>;

/// @brief Presents a free-text question using the TUI QuestionComponent.
///
/// Creates and manages Terminal + Screen (Inline viewport) internally.
/// @param question The question text to display.
/// @param masked If true, input is masked (for API keys, passwords).
/// @return The entered text, or std::nullopt on cancel/Esc.
[[nodiscard]] auto askFreeText(std::string_view question, bool masked = false) -> std::optional<std::string>;

/// @brief Opens a URL in the system's default browser.
/// @param url The URL to open.
/// @return true if the browser was launched successfully.
[[nodiscard]] auto openBrowser(std::string_view url) -> bool;

} // namespace endo::agent
