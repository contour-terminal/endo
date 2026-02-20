// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/completion/CompletionProvider.hpp>

#include <string>
#include <vector>

namespace endo
{

/// @brief Completion provider for command options/flags.
///
/// This is a stub implementation. Future versions may parse --help output
/// or use a database of known command options.
class OptionCompleter: public CompletionProvider
{
  public:
    [[nodiscard]] std::vector<CompletionItem> complete(CompletionContext const& context) override;
    [[nodiscard]] bool canHandle(CompletionContextType type) const override;

    [[nodiscard]] int priority() const override { return 60; }
};

} // namespace endo
