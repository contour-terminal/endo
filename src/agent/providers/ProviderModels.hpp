// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <locale>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <agent/Types.hpp>

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

/// Hardcoded list of available GitHub Copilot models (newest first).
inline constexpr std::array CopilotModels = {
    std::string_view { "gpt-4o" },
    std::string_view { "gpt-4o-mini" },
    std::string_view { "claude-3.5-sonnet" },
    std::string_view { "o3-mini" },
};

/// All known provider names in display order.
inline constexpr std::array KnownProviders = {
    std::string_view { "claude" },
    std::string_view { "openai" },
    std::string_view { "gemini" },
    std::string_view { "copilot" },
    std::string_view { "local" },
};

/// Result of a model name lookup across all known providers.
struct ModelMatch
{
    std::string_view providerName; ///< The provider that owns this model.
    std::string_view modelName;    ///< The full model identifier.
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
    if (providerName == "copilot")
        return CopilotModels;
    return {};
}

/// Returns a flat list of all known (provider, model) pairs across all providers.
/// @return A vector of ModelMatch entries in provider order.
[[nodiscard]] inline auto allKnownModels() -> std::vector<ModelMatch>
{
    auto result = std::vector<ModelMatch> {};
    for (auto const provider: KnownProviders)
        for (auto const model: modelsForProvider(provider))
            result.push_back(ModelMatch { .providerName = provider, .modelName = model });
    return result;
}

/// Finds a model by name using case-insensitive substring matching.
///
/// Exact matches take priority over substring matches. When multiple substring matches
/// exist, the preferredProvider is used for disambiguation.
/// @param query The search query (e.g. "sonnet", "gpt-4o", "claude-opus-4-6").
/// @param preferredProvider Provider to prefer when disambiguating (e.g. the active provider).
/// @return The matching model, or nullopt if no match is found.
[[nodiscard]] inline auto findModelByName(std::string_view query, std::string_view preferredProvider = {})
    -> std::optional<ModelMatch>
{
    if (query.empty())
        return std::nullopt;

    // Convert query to lowercase for case-insensitive matching.
    auto lowerQuery = std::string(query);
    std::ranges::transform(lowerQuery, lowerQuery.begin(), [](unsigned char c) { return std::tolower(c); });

    auto const allModels = allKnownModels();

    // First pass: exact match (case-insensitive).
    for (auto const& m: allModels)
    {
        auto lowerModel = std::string(m.modelName);
        std::ranges::transform(
            lowerModel, lowerModel.begin(), [](unsigned char c) { return std::tolower(c); });
        if (lowerModel == lowerQuery)
            return m;
    }

    // Second pass: substring matches, prefer preferredProvider.
    auto matches = std::vector<ModelMatch> {};
    for (auto const& m: allModels)
    {
        auto lowerModel = std::string(m.modelName);
        std::ranges::transform(
            lowerModel, lowerModel.begin(), [](unsigned char c) { return std::tolower(c); });
        if (lowerModel.find(lowerQuery) != std::string::npos)
            matches.push_back(m);
    }

    if (matches.empty())
        return std::nullopt;

    // If there's exactly one match, return it.
    if (matches.size() == 1)
        return matches.front();

    // Prefer the match from the preferred provider.
    for (auto const& m: matches)
        if (m.providerName == preferredProvider)
            return m;

    // Fall back to first match in provider order.
    return matches.front();
}

/// Formats a capability diff between two models as a markdown string.
///
/// Shows a confirmation line and, if capabilities differ, a comparison table
/// highlighting only the changed fields.
/// @param oldInfo The previous model's info.
/// @param newInfo The new model's info.
/// @return Markdown-formatted comparison string.
[[nodiscard]] inline auto formatCapabilityDiff(ModelInfo const& oldInfo, ModelInfo const& newInfo)
    -> std::string
{
    auto result = std::format("Switched from **{}** ({}) to **{}** ({}).\n",
                              oldInfo.modelName,
                              oldInfo.providerName,
                              newInfo.modelName,
                              newInfo.providerName);

    // Collect changed capabilities.
    struct DiffRow
    {
        std::string capability;
        std::string oldValue;
        std::string newValue;
    };

    auto diffs = std::vector<DiffRow> {};

    if (oldInfo.contextSize != newInfo.contextSize)
        diffs.push_back({ "Context size",
                          std::format(std::locale(""), "{:L}", oldInfo.contextSize),
                          std::format(std::locale(""), "{:L}", newInfo.contextSize) });
    auto const boolStr = [](bool v) -> std::string_view {
        return v ? "\u2705" : "\u274C";
    };
    if (oldInfo.supportsToolUse != newInfo.supportsToolUse)
        diffs.push_back({ "Tool use",
                          std::string(boolStr(oldInfo.supportsToolUse)),
                          std::string(boolStr(newInfo.supportsToolUse)) });
    if (oldInfo.supportsImageInput != newInfo.supportsImageInput)
        diffs.push_back({ "Image input",
                          std::string(boolStr(oldInfo.supportsImageInput)),
                          std::string(boolStr(newInfo.supportsImageInput)) });
    if (oldInfo.supportsImageOutput != newInfo.supportsImageOutput)
        diffs.push_back({ "Image output",
                          std::string(boolStr(oldInfo.supportsImageOutput)),
                          std::string(boolStr(newInfo.supportsImageOutput)) });

    if (!diffs.empty())
    {
        result += "\n| Capability | Before | After |\n|:-----------|:-------|:------|\n";
        for (auto const& row: diffs)
            result += std::format("| {} | {} | {} |\n", row.capability, row.oldValue, row.newValue);
    }

    return result;
}

} // namespace endo::agent
