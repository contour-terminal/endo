// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

namespace endo::platform
{

/// @brief Returns the local machine's hostname.
///
/// Encapsulates the per-platform system call (`GetComputerNameA` on Windows,
/// `gethostname` on POSIX) so business logic stays free of platform `#ifdef`s.
///
/// @return The hostname, or an empty string if it could not be determined.
[[nodiscard]] std::string hostName();

} // namespace endo::platform
