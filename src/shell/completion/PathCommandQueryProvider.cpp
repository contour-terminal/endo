// SPDX-License-Identifier: Apache-2.0
#include "PathCommandQueryProvider.hpp"

namespace endo
{

PathCommandQueryProvider::PathCommandQueryProvider(PathCommandIndex const& index,
                                                   EnvironmentProvider const& env):
    _index(index), _env(env)
{
}

std::vector<QueryResult> PathCommandQueryProvider::query(std::string_view queryTag)
{
    if (queryTag != "path-commands")
        return {};

    auto const& entries = _index.entries();
    auto const home = homeDirectory(_env);

    auto results = std::vector<QueryResult> {};
    results.reserve(entries.size());
    for (auto const& [name, path]: entries)
        results.push_back(QueryResult { .text = name, .description = collapseHomePrefix(path, home) });

    // entries() is already sorted by name, so the result is too.
    return results;
}

} // namespace endo
