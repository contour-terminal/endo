// SPDX-License-Identifier: Apache-2.0
#include "Types.hpp"

#include <format>

namespace endo::agent
{

namespace
{
    /// Per-million-token pricing for a model.
    struct ModelPricing
    {
        double inputPerMillion = 0.0;         ///< Cost per million input tokens.
        double outputPerMillion = 0.0;        ///< Cost per million output tokens.
        double cacheReadDiscount = 0.90;      ///< Fraction discount for cache reads (0.90 = 90% off).
        double cacheCreationMultiplier = 1.0; ///< Multiplier for cache creation (1.25 = 25% surcharge).
    };

    /// Returns pricing for a known model, or nullopt for unknown models.
    auto lookupPricing(std::string_view providerName, std::string_view modelName)
        -> std::optional<ModelPricing>
    {
        if (providerName == "claude")
        {
            if (modelName.find("opus") != std::string_view::npos)
                return ModelPricing { .inputPerMillion = 15.0,
                                      .outputPerMillion = 75.0,
                                      .cacheReadDiscount = 0.90,
                                      .cacheCreationMultiplier = 1.25 };
            if (modelName.find("sonnet") != std::string_view::npos)
                return ModelPricing { .inputPerMillion = 3.0,
                                      .outputPerMillion = 15.0,
                                      .cacheReadDiscount = 0.90,
                                      .cacheCreationMultiplier = 1.25 };
            if (modelName.find("haiku") != std::string_view::npos)
                return ModelPricing { .inputPerMillion = 0.80,
                                      .outputPerMillion = 4.0,
                                      .cacheReadDiscount = 0.90,
                                      .cacheCreationMultiplier = 1.25 };
        }
        else if (providerName == "openai")
        {
            if (modelName.find("gpt-4o-mini") != std::string_view::npos)
                return ModelPricing { .inputPerMillion = 0.15,
                                      .outputPerMillion = 0.60,
                                      .cacheReadDiscount = 0.50,
                                      .cacheCreationMultiplier = 1.0 };
            if (modelName.find("gpt-4o") != std::string_view::npos)
                return ModelPricing { .inputPerMillion = 2.50,
                                      .outputPerMillion = 10.0,
                                      .cacheReadDiscount = 0.50,
                                      .cacheCreationMultiplier = 1.0 };
        }
        else if (providerName == "gemini")
        {
            if (modelName.find("flash") != std::string_view::npos)
                return ModelPricing { .inputPerMillion = 0.15,
                                      .outputPerMillion = 0.60,
                                      .cacheReadDiscount = 0.0,
                                      .cacheCreationMultiplier = 1.0 };
            if (modelName.find("pro") != std::string_view::npos)
                return ModelPricing { .inputPerMillion = 1.25,
                                      .outputPerMillion = 10.0,
                                      .cacheReadDiscount = 0.0,
                                      .cacheCreationMultiplier = 1.0 };
        }

        return std::nullopt;
    }
} // namespace

auto estimateCost(TokenUsage const& usage, std::string_view providerName, std::string_view modelName)
    -> double
{
    auto const pricing = lookupPricing(providerName, modelName);
    if (!pricing.has_value())
        return 0.0;

    auto const& p = *pricing;
    auto const million = 1'000'000.0;

    // Regular input tokens (excluding cached)
    auto const regularInput = usage.inputTokens - usage.cacheReadTokens;
    auto const inputCost = (static_cast<double>(regularInput) / million) * p.inputPerMillion;

    // Cache reads: discounted rate
    auto const cacheReadRate = p.inputPerMillion * (1.0 - p.cacheReadDiscount);
    auto const cacheReadCost = (static_cast<double>(usage.cacheReadTokens) / million) * cacheReadRate;

    // Cache creation: surcharge rate
    auto const cacheCreateRate = p.inputPerMillion * p.cacheCreationMultiplier;
    auto const cacheCreateCost = (static_cast<double>(usage.cacheCreationTokens) / million) * cacheCreateRate;

    // Output tokens
    auto const outputCost = (static_cast<double>(usage.outputTokens) / million) * p.outputPerMillion;

    return inputCost + cacheReadCost + cacheCreateCost + outputCost;
}

auto formatTokenCount(int64_t count) -> std::string
{
    if (count < 0)
        return std::format("-{}", formatTokenCount(-count));
    if (count < 1000)
        return std::format("{}", count);
    if (count < 10'000)
        return std::format("{:.1f}k", static_cast<double>(count) / 1000.0);
    if (count < 1'000'000)
        return std::format("{:.0f}k", static_cast<double>(count) / 1000.0);
    return std::format("{:.1f}M", static_cast<double>(count) / 1'000'000.0);
}

} // namespace endo::agent
