// SPDX-License-Identifier: Apache-2.0
#include "ProcessNameQueryProvider.hpp"

#include <algorithm>
#include <format>
#include <unordered_map>

namespace endo
{

ProcessNameQueryProvider::ProcessNameQueryProvider(ProcessProvider const& provider): _provider(provider)
{
}

std::vector<QueryResult> ProcessNameQueryProvider::query(std::string_view queryTag)
{
    if (queryTag != "process-names" && queryTag != "process-command-lines")
        return {};

    // Deduplicate by command text; keep a representative pid for the description
    // so the menu can show e.g. "pid=1234" to disambiguate.
    auto seen = std::unordered_map<std::string, int64_t> {};
    for (auto const& entry: _provider.listProcesses())
    {
        if (entry.command.empty())
            continue;
        seen.try_emplace(entry.command, entry.pid);
    }

    auto results = std::vector<QueryResult> {};
    results.reserve(seen.size());
    for (auto const& [cmd, pid]: seen)
        results.push_back(QueryResult { .text = cmd, .description = std::format("pid={}", pid) });

    std::ranges::sort(results, {}, &QueryResult::text);
    return results;
}

} // namespace endo
