// SPDX-License-Identifier: Apache-2.0
#include "BindCommand.hpp"

#include <CoreVM/CoreVM.hpp>
#include <CoreVM/types/TypeDescriptor.hpp>

#include <ranges>

namespace endo
{

BindCommand::BindCommand(tui::KeyBindings const& bindings): _bindings(bindings)
{
}

uint16_t BindCommand::outputTypeId() const
{
    return CoreVM::BuiltinTypeId::KeyBindingInfo;
}

CoreVM::TypedObject* BindCommand::execute(CoreVM::Runner& runner) const
{
    auto const bindings = _bindings.bindings();

    // Start with Nil (empty list)
    auto* list = runner.allocObject(CoreVM::BuiltinTypeId::List);
    list->tag = 0; // Nil

    // Build cons-cell list right-to-left so the result is in original order
    for (auto const& [chord, action]: std::ranges::reverse_view(bindings))
    {
        // Allocate a KeyBindingInfo record
        auto* record = runner.allocObject(CoreVM::BuiltinTypeId::KeyBindingInfo);
        record->setSlot(0, reinterpret_cast<uintptr_t>(runner.newString(chord.toString())));
        record->setSlot(
            1, reinterpret_cast<uintptr_t>(runner.newString(std::string(tui::editActionToString(action)))));

        // Cons this record onto the list
        auto* cons = runner.allocObject(CoreVM::BuiltinTypeId::List);
        cons->tag = 1; // Cons
        cons->setSlot(0, reinterpret_cast<uintptr_t>(record));
        cons->setSlot(1, reinterpret_cast<uintptr_t>(list));
        list = cons;
    }

    return list;
}

} // namespace endo
