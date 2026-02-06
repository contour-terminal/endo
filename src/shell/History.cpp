// SPDX-License-Identifier: Apache-2.0
#include "History.hpp"

#include <algorithm>

namespace endo
{

InMemoryHistory::InMemoryHistory(size_t maxSize): _maxSize(maxSize)
{
    _entries.reserve(std::min(maxSize, size_t { 256 }));
}

void InMemoryHistory::add(std::string entry)
{
    if (entry.empty())
        return;

    // Don't add duplicates of the most recent entry
    if (!_entries.empty() && _entries.back() == entry)
        return;

    // Remove oldest entries if at capacity
    if (_entries.size() >= _maxSize)
        _entries.erase(_entries.begin());

    _entries.push_back(std::move(entry));
}

std::vector<std::string> const& InMemoryHistory::entries() const
{
    return _entries;
}

size_t InMemoryHistory::size() const
{
    return _entries.size();
}

size_t InMemoryHistory::maxSize() const
{
    return _maxSize;
}

void InMemoryHistory::clear()
{
    _entries.clear();
}

std::vector<std::string_view> InMemoryHistory::search(std::string_view prefix, size_t maxResults) const
{
    std::vector<std::string_view> results;
    results.reserve(std::min(maxResults, _entries.size()));

    // Search from newest to oldest (reverse order)
    for (auto it = _entries.rbegin(); it != _entries.rend() && results.size() < maxResults; ++it)
    {
        if (it->starts_with(prefix))
        {
            // Avoid duplicates in results
            bool isDuplicate = false;
            for (auto const& existing: results)
            {
                if (existing == *it)
                {
                    isDuplicate = true;
                    break;
                }
            }
            if (!isDuplicate)
                results.emplace_back(*it);
        }
    }

    return results;
}

} // namespace endo
