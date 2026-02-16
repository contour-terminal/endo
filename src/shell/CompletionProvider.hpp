// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <endo-language/CompletionContext.hpp>

#include <tui/completer/CompletionItem.hpp>

#include <string>
#include <vector>

namespace endo
{

// Use tui::CompletionItem for completion items
using tui::CompletionItem;

/// @brief Abstract base for completion providers.
///
/// Completion providers generate suggestions for a specific type of context.
/// Multiple providers can be registered with the Completer orchestrator.
class CompletionProvider
{
  public:
    virtual ~CompletionProvider() = default;

    /// @brief Generates completions for the given context.
    /// @param context The completion context.
    /// @return List of completion items (may be empty).
    [[nodiscard]] virtual std::vector<CompletionItem> complete(CompletionContext const& context) = 0;

    /// @brief Checks if this provider can handle the given context type.
    /// @param type The context type.
    /// @return true if this provider handles the context type.
    [[nodiscard]] virtual bool canHandle(CompletionContextType type) const = 0;

    /// @brief Returns the priority of this provider (higher = checked first).
    /// @return Priority value (default: 0).
    [[nodiscard]] virtual int priority() const { return 0; }

    /// @brief Returns true if this provider's results should suppress lower-priority providers.
    ///
    /// When a provider returns non-empty results and isExclusiveFor() returns true,
    /// the Completer stops querying lower-priority providers. This prevents e.g.
    /// FileCompleter from adding directory suggestions when CommandSpecCompleter
    /// is completing option values (like preset names).
    /// @param context The completion context.
    /// @return true if results are exclusive for this context (default: false).
    [[nodiscard]] virtual bool isExclusiveFor(CompletionContext const& context) const
    {
        (void) context;
        return false;
    }
};

} // namespace endo
