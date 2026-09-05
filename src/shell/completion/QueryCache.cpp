// SPDX-License-Identifier: Apache-2.0
#include "QueryCache.hpp"

namespace endo
{

QueryCache::QueryCache(std::unique_ptr<CommandQueryProvider> provider, std::chrono::milliseconds ttl):
    _provider(std::move(provider)), _ttl(ttl)
{
}

std::vector<QueryResult> const& QueryCache::query(std::string_view queryTag)
{
    auto const key = std::string(queryTag);
    auto const now = std::chrono::steady_clock::now();

    if (auto it = _entries.find(key); it != _entries.end())
    {
        auto const age = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second.timestamp);
        if (age < _ttl)
            return it->second.data;
    }

    auto& entry = _entries[key];
    entry.data = _provider->query(queryTag);
    entry.timestamp = now;
    return entry.data;
}

void QueryCache::invalidateAll()
{
    _entries.clear();
}

} // namespace endo
