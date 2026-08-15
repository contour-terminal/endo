// SPDX-License-Identifier: Apache-2.0
#include "BindCommand.hpp"

#include <endo-language/builtins/RecordWriter.hpp>

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

    auto* list = runner.makeNilList(CoreVM::LiteralType::Object);

    // Build cons-cell list right-to-left so the result is in original order
    for (auto const& [chord, action]: std::ranges::reverse_view(bindings))
    {
        auto writer = builtins::RecordWriter { &runner, CoreVM::BuiltinTypeId::KeyBindingInfo };
        auto* record =
            writer.set("key", chord.toString()).set("action", tui::editActionToString(action)).record();

        list = runner.makeConsCell(reinterpret_cast<uintptr_t>(record), list, CoreVM::LiteralType::Object);
    }

    return list;
}

} // namespace endo
