// SPDX-License-Identifier: Apache-2.0
#include <endo-language/builtins/FileManager.hpp>

namespace endo
{

std::optional<int64_t> FileManager::open(std::string const& path, std::string const& mode)
{
    auto openMode = std::ios::binary;
    if (mode == "r")
        openMode |= std::ios::in;
    else if (mode == "w")
        openMode |= std::ios::out | std::ios::trunc;
    else if (mode == "a")
        openMode |= std::ios::out | std::ios::app;
    else if (mode == "rw")
        openMode |= std::ios::in | std::ios::out;
    else
        return std::nullopt;

    auto stream = std::make_unique<std::fstream>(path, openMode);
    if (!stream->is_open())
        return std::nullopt;

    auto const handle = _nextHandle++;
    _handles[handle] = std::move(stream);
    return handle;
}

bool FileManager::close(int64_t handle)
{
    auto it = _handles.find(handle);
    if (it == _handles.end())
        return false;

    it->second->close();
    _handles.erase(it);
    return true;
}

std::optional<std::string> FileManager::readLine(int64_t handle)
{
    auto it = _handles.find(handle);
    if (it == _handles.end())
        return std::nullopt;

    std::string line;
    if (!std::getline(*it->second, line))
        return std::nullopt;

    return line;
}

bool FileManager::isOpen(int64_t handle) const
{
    auto it = _handles.find(handle);
    return it != _handles.end() && it->second->is_open();
}

} // namespace endo
