// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/PromptModule.hpp>

#include <string>

namespace endo
{

/// @brief Prompt module that renders the cursor-line indicator (e.g., "> ", "λ ", "|> ").
///
/// The indicator changes color based on the last command's exit code.
class IndicatorModule final: public PromptModule
{
  public:
    /// @brief Constructs an IndicatorModule with the given indicator string.
    /// @param indicator The indicator characters to display.
    explicit IndicatorModule(std::string indicator = "> "): _indicator(std::move(indicator)) {}

    [[nodiscard]] std::string_view id() const noexcept override { return "indicator"; }

    [[nodiscard]] PromptSegments evaluate(PromptContext const& ctx) const override;

    /// @brief Sets the indicator string.
    void setIndicator(std::string indicator) { _indicator = std::move(indicator); }

  private:
    std::string _indicator;
};

} // namespace endo
