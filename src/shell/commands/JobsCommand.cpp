// SPDX-License-Identifier: Apache-2.0
#include "JobsCommand.hpp"

#include <endo-language/builtins/RecordWriter.hpp>

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

    auto* list = runner.makeNilList(CoreVM::LiteralType::Object);

    // Build cons-cell list right-to-left so the result is in original order
    for (const auto& job: std::ranges::reverse_view(jobs))
    {
        auto writer = builtins::RecordWriter { &runner, CoreVM::BuiltinTypeId::JobInfo };
        auto* record = writer.set("id", job.id)
                           .set("state", job.state)
                           .set("command", job.command)
                           .set("pid", job.pid)
                           .record();

        list = runner.makeConsCell(reinterpret_cast<uintptr_t>(record), list, CoreVM::LiteralType::Object);
    }

    return list;
}

} // namespace endo
