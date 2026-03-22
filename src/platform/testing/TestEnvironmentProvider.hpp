// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <map>
#include <set>
#include <string>
#include <utility>

#include <platform/EnvironmentProvider.hpp>

namespace endo::platform
{

/// @brief Fully isolated test environment for unit testing.
///
/// Unlike the old TestEnvironment, this implementation does NOT fall back
/// to getenv() or call setenv()/unsetenv(). All state is kept in memory,
/// ensuring complete test isolation from the real OS environment.
class TestEnvironmentProvider final: public EnvironmentProvider
{
  public:
    /// @param initialCwd Initial working directory for the test environment.
    explicit TestEnvironmentProvider(std::string initialCwd = "/home/testuser"):
        _currentDirectory(std::move(initialCwd))
    {
    }

    void set(std::string_view name, std::string_view value) override
    {
        _values[std::string(name)] = std::string(value);
    }

    [[nodiscard]] std::optional<std::string> get(std::string_view name) const override
    {
        if (auto const i = _values.find(std::string(name)); i != _values.end())
            return i->second;
        return std::nullopt;
    }

    void unset(std::string_view name) override { _values.erase(std::string(name)); }

    void exportVariable(std::string_view /*name*/) override
    {
        // No-op in test environment — no real OS export
    }

    [[nodiscard]] std::vector<std::string> keys() const override
    {
        std::vector<std::string> result;
        result.reserve(_values.size());
        for (auto const& [key, _]: _values)
            result.push_back(key);
        return result;
    }

    [[nodiscard]] std::expected<void, PlatformError> changeDirectory(
        std::filesystem::path const& path) override
    {
        auto const resolved = path.is_absolute() ? path : std::filesystem::path(_currentDirectory) / path;
        auto const pathStr = resolved.lexically_normal().generic_string();
        if (!_validPaths.empty() && !_validPaths.contains(pathStr))
            return std::unexpected(PlatformError::FileNotFound);
        _currentDirectory = pathStr;
        return {};
    }

    [[nodiscard]] std::string currentDirectory() const override { return _currentDirectory; }

    /// Adds a path that changeDirectory will accept.
    /// If no valid paths have been added, all paths are accepted.
    void addValidPath(std::string const& path)
    {
        _validPaths.insert(std::filesystem::path(path).lexically_normal().generic_string());
    }

  private:
    std::map<std::string, std::string> _values;
    std::string _currentDirectory;
    std::set<std::string> _validPaths;
};

} // namespace endo::platform

// Backward-compatible aliases
namespace endo
{
using endo::platform::TestEnvironmentProvider;
using TestEnvironment = endo::platform::TestEnvironmentProvider;
} // namespace endo
