// SPDX-License-Identifier: Apache-2.0
#include "PsCommand.hpp"

#include <endo-language/builtins/BuiltinImpls.hpp>
#include <endo-language/builtins/RecordWriter.hpp>

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

    auto* list = runner.makeNilList(CoreVM::LiteralType::Object);

    // Build cons-cell list right-to-left so the result is in original order
    for (const auto& proc: std::ranges::reverse_view(processes))
    {
        auto writer = builtins::RecordWriter { &runner, CoreVM::BuiltinTypeId::ProcessInfo };
        auto* record =
            writer.set("pid", proc.pid)
                .set("ppid", proc.ppid)
                .set("user", proc.user)
                .set("cpu", proc.cpuPercent)
                .set("mem", builtins::makeSizeFromBytes(&runner, proc.memKb * static_cast<int64_t>(1024)))
                .set("command", proc.command)
                .record();

        list = runner.makeConsCell(reinterpret_cast<uintptr_t>(record), list, CoreVM::LiteralType::Object);
    }

    return list;
}

} // namespace endo
