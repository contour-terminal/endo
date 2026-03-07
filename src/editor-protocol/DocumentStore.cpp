// SPDX-License-Identifier: Apache-2.0
#include "DocumentStore.hpp"

namespace endo::editor_protocol
{

void DocumentStore::open(std::string const& uri, std::string text, int version)
{
    _documents[uri] = Document { .text = std::move(text), .version = version };
}

void DocumentStore::update(std::string const& uri, std::string text, int version)
{
    if (auto it = _documents.find(uri); it != _documents.end())
    {
        it->second.text = std::move(text);
        it->second.version = version;
    }
}

void DocumentStore::close(std::string const& uri)
{
    _documents.erase(uri);
}

std::string const* DocumentStore::get(std::string const& uri) const
{
    auto const it = _documents.find(uri);
    if (it == _documents.end())
        return nullptr;
    return &it->second.text;
}

int DocumentStore::version(std::string const& uri) const
{
    auto const it = _documents.find(uri);
    if (it == _documents.end())
        return -1;
    return it->second.version;
}

std::vector<std::string> DocumentStore::uris() const
{
    std::vector<std::string> result;
    result.reserve(_documents.size());
    for (auto const& [uri, _]: _documents)
        result.push_back(uri);
    return result;
}

} // namespace endo::editor_protocol
