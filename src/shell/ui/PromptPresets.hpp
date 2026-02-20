// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/ui/PromptConfig.hpp>

#include <string_view>
#include <vector>

namespace endo
{

/// @brief Returns a prompt configuration for the named preset.
/// @param name The preset name (e.g., "opencode-bar", "endo-signature").
/// @return The corresponding PromptConfig, or the default if name is unknown.
[[nodiscard]] PromptConfig promptPreset(std::string_view name);

/// @brief Returns the list of all available preset names.
[[nodiscard]] std::vector<std::string_view> promptPresetNames();

} // namespace endo
