// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#ifdef _WIN32
    #include <stdlib.h> // _putenv_s
#endif

namespace endo::testing
{

/// @brief Cross-platform setenv for tests.
/// @param name Environment variable name.
/// @param value Environment variable value.
inline void setTestEnv(char const* name, char const* value)
{
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

/// @brief Cross-platform unsetenv for tests.
/// @param name Environment variable name to unset.
inline void unsetTestEnv(char const* name)
{
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

/// @brief Sets an environment variable for a scope, restoring the previous value after.
///
/// The environment is process-global, so a test that sets a variable and restores it at the
/// end of the test body leaks that change whenever an assertion throws first. $HOME is the
/// one that bites here: other fixtures read it while constructing a shell, so a leaked
/// value silently redirects a later test's history or config to the wrong place.
class ScopedEnv
{
  public:
    /// @brief Sets @p name to @p value until this object goes out of scope.
    /// @param name  Environment variable name.
    /// @param value Value to set for the duration of the scope.
    ScopedEnv(std::string_view name, std::string_view value): _name { name }
    {
        if (auto const* previous = std::getenv(_name.c_str()))
            _previous = std::string { previous };
        setTestEnv(_name.c_str(), std::string { value }.c_str());
    }

    ~ScopedEnv()
    {
        if (_previous)
            setTestEnv(_name.c_str(), _previous->c_str());
        else
            unsetTestEnv(_name.c_str());
    }

    ScopedEnv(ScopedEnv const&) = delete;
    ScopedEnv& operator=(ScopedEnv const&) = delete;
    ScopedEnv(ScopedEnv&&) = delete;
    ScopedEnv& operator=(ScopedEnv&&) = delete;

  private:
    std::string _name;
    std::optional<std::string> _previous;
};

} // namespace endo::testing
