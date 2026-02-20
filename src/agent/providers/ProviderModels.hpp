// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <algorithm>
#include <array>
#include <span>
#include <string_view>

namespace endo::agent
{

/// Hardcoded list of available Claude models (newest first).
inline constexpr std::array ClaudeModels = {
    std::string_view { "claude-opus-4-6" },           std::string_view { "claude-sonnet-4-6" },
    std::string_view { "claude-haiku-4-5-20251001" }, std::string_view { "claude-sonnet-4-5-20250929" },
    std::string_view { "claude-opus-4-20250514" },
};

/// Hardcoded list of available OpenAI models (newest first).
inline constexpr std::array OpenAiModels = {
    std::string_view { "gpt-4o" },
    std::string_view { "gpt-4o-mini" },
    std::string_view { "o3-mini" },
    std::string_view { "o1" },
};

/// Hardcoded list of available Gemini models (newest first).
inline constexpr std::array GeminiModels = {
    std::string_view { "gemini-2.5-flash" },
    std::string_view { "gemini-2.5-pro" },
    std::string_view { "gemini-2.0-flash" },
};

/// Returns the next model in the given model list after the current one, wrapping around.
///
/// If the current model is not found in the list, returns the first model.
/// @param models The model list to cycle through (span or array).
/// @param currentModel The currently active model identifier.
/// @return The next model identifier in the cycle.
[[nodiscard]] inline auto nextModel(std::span<std::string_view const> models,
                                    std::string_view currentModel) noexcept -> std::string_view
{
    if (models.empty())
        return currentModel;
    auto const it = std::ranges::find(models, currentModel);
    if (it == models.end())
        return models.front();
    auto const nextIt = std::next(it);
    if (nextIt == models.end())
        return models.front();
    return *nextIt;
}

/// Returns the model list for a given provider name.
/// @param providerName The provider identifier ("claude", "openai", "gemini").
/// @return A span over the model list, or an empty span if the provider is unknown.
[[nodiscard]] inline auto modelsForProvider(std::string_view providerName) noexcept
    -> std::span<std::string_view const>
{
    if (providerName == "claude")
        return ClaudeModels;
    if (providerName == "openai")
        return OpenAiModels;
    if (providerName == "gemini")
        return GeminiModels;
    return {};
}

} // namespace endo::agent
