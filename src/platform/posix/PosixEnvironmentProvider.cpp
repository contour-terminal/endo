// SPDX-License-Identifier: Apache-2.0
#include "PosixEnvironmentProvider.hpp"

#include <platform/PathUtils.hpp>

#include <algorithm>
#include <filesystem>

#include <unistd.h>

#if defined(__APPLE__)
    #include <crt_externs.h>
    #define environ (*_NSGetEnviron())
#endif

namespace endo::platform
{

PosixEnvironmentProvider& PosixEnvironmentProvider::instance()
{
    static PosixEnvironmentProvider env;
    return env;
}

void PosixEnvironmentProvider::set(std::string_view name, std::string_view value)
{
    _values[std::string(name)] = std::string(value);
}

std::optional<std::string> PosixEnvironmentProvider::get(std::string_view name) const
{
    if (auto i = _values.find(std::string(name)); i != _values.end())
        return i->second;
    else if (auto const* value = getenv(std::string(name).c_str()))
        return std::string { value };
    else
        return std::nullopt;
}

void PosixEnvironmentProvider::unset(std::string_view name)
{
    _values.erase(std::string(name));
    unsetenv(std::string(name).c_str());
}

void PosixEnvironmentProvider::exportVariable(std::string_view name)
{
    if (auto i = _values.find(std::string(name)); i != _values.end())
        setenv(std::string(name).c_str(), i->second.c_str(), 1);
}

std::vector<std::string> PosixEnvironmentProvider::keys() const
{
    std::vector<std::string> result;

    // First, collect from system environment
    for (char** env = environ; *env != nullptr; ++env)
    {
        std::string_view const entry(*env);
        if (auto const pos = entry.find('='); pos != std::string_view::npos)
            result.emplace_back(entry.substr(0, pos));
    }

    // Add locally-set variables that might not be exported yet
    for (auto const& [key, _]: _values)
    {
        if (std::ranges::find(result, key) == result.end())
            result.push_back(key);
    }

    return result;
}

std::expected<void, PlatformError> PosixEnvironmentProvider::changeDirectory(
    std::filesystem::path const& path)
{
    if (chdir(path.c_str()) != 0)
        return std::unexpected(PlatformError::FileNotFound);
    return {};
}

std::string PosixEnvironmentProvider::currentDirectory() const
{
    return normalizePath(std::filesystem::current_path());
}

} // namespace endo::platform
