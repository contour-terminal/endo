// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/PromptModule.hpp>

namespace endo
{

/// @brief Prompt module that detects project toolchain from project files.
///
/// Detects common project types by looking for Cargo.toml, package.json,
/// CMakeLists.txt, go.mod, etc. in the current directory.
class ToolchainModule final: public PromptModule
{
  public:
    [[nodiscard]] std::string_view id() const noexcept override { return "toolchain"; }

    [[nodiscard]] PromptSegments evaluate(PromptContext const& ctx) const override;
    [[nodiscard]] bool shouldShow(PromptContext const& ctx) const override;
};

} // namespace endo
