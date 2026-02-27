// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdlib>
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

} // namespace endo::testing
