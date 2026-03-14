// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tui/KeyBindings.hpp>

#include "StructuredCommand.hpp"

namespace endo
{

/// Built-in `bind` command that returns keybinding information as a list of KeyBindingInfo records.
///
/// Produces structured output from the shell's current key bindings,
/// enabling pipeline composition like `bind |> filter (.action == "undo")`.
class BindCommand final: public StructuredCommand
{
  public:
    /// Constructs a BindCommand with the given key bindings.
    /// @param bindings The shell's current key bindings.
    explicit BindCommand(tui::KeyBindings const& bindings);

    /// @return BuiltinTypeId::KeyBindingInfo
    [[nodiscard]] uint16_t outputTypeId() const override;

    /// Executes the bind command, returning a list<KeyBindingInfo>.
    /// @param runner The VM runner for allocating objects.
    /// @return A cons-cell list of KeyBindingInfo TypedObject records.
    [[nodiscard]] CoreVM::TypedObject* execute(CoreVM::Runner& runner) const override;

  private:
    tui::KeyBindings const& _bindings;
};

} // namespace endo
