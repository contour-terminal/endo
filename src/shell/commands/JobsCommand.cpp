// SPDX-License-Identifier: Apache-2.0
#include "JobsCommand.hpp"

#include <CoreVM/CoreVM.hpp>
#include <CoreVM/types/TypeDescriptor.hpp>

#include <ranges>

namespace endo
{

JobsCommand::JobsCommand(JobProvider const& provider): _provider(provider)
{
}

uint16_t JobsCommand::outputTypeId() const
{
    return CoreVM::BuiltinTypeId::JobInfo;
}

CoreVM::TypedObject* JobsCommand::execute(CoreVM::Runner& runner) const
{
    auto const jobs = _provider.listJobs();

    // Start with Nil (empty list)
    auto* list = runner.allocObject(CoreVM::BuiltinTypeId::List);
    list->tag = 0; // Nil

    // Build cons-cell list right-to-left so the result is in original order
    for (const auto& job: std::ranges::reverse_view(jobs))
    {
        // Allocate a JobInfo record
        auto* record = runner.allocObject(CoreVM::BuiltinTypeId::JobInfo);
        record->setSlot(0, static_cast<uint64_t>(job.id));
        record->setSlot(1, reinterpret_cast<uintptr_t>(runner.newString(job.state)));
        record->setSlot(2, reinterpret_cast<uintptr_t>(runner.newString(job.command)));
        record->setSlot(3, static_cast<uint64_t>(job.pid));

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
