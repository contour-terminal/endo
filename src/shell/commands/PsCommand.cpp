// SPDX-License-Identifier: Apache-2.0
#include "PsCommand.hpp"

#include <endo-language/builtins/BuiltinImpls.hpp>

#include <CoreVM/CoreVM.hpp>
#include <CoreVM/types/TypeDescriptor.hpp>

#include <bit>
#include <ranges>

namespace endo
{

PsCommand::PsCommand(ProcessProvider const& provider): _provider(provider)
{
}

uint16_t PsCommand::outputTypeId() const
{
    return CoreVM::BuiltinTypeId::ProcessInfo;
}

CoreVM::TypedObject* PsCommand::execute(CoreVM::Runner& runner) const
{
    auto const processes = _provider.listProcesses();

    // Start with Nil (empty list)
    auto* list = runner.allocObject(CoreVM::BuiltinTypeId::List);
    list->tag = 0; // Nil

    // Build cons-cell list right-to-left so the result is in original order
    for (const auto& proc: std::ranges::reverse_view(processes))
    {
        // Allocate a ProcessInfo record
        auto* record = runner.allocObject(CoreVM::BuiltinTypeId::ProcessInfo);
        record->setSlot(0, static_cast<uint64_t>(proc.pid));
        record->setSlot(1, static_cast<uint64_t>(proc.ppid));
        record->setSlot(2, reinterpret_cast<uintptr_t>(runner.newString(proc.user)));
        record->setSlot(3, std::bit_cast<uint64_t>(proc.cpuPercent));
        auto* memSize = builtins::makeSizeFromBytes(&runner, proc.memKb * static_cast<int64_t>(1024));
        record->setSlot(4, reinterpret_cast<uintptr_t>(memSize));
        record->setSlot(5, reinterpret_cast<uintptr_t>(runner.newString(proc.command)));

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
