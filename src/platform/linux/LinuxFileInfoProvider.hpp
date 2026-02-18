// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <platform/FileInfoProvider.hpp>

namespace endo::platform
{

/// Linux/POSIX implementation of FileInfoProvider using std::filesystem.
///
/// Uses directory_iterator for non-recursive listing and stat for metadata.
/// Gracefully skips unreadable entries via std::error_code overloads.
class LinuxFileInfoProvider final: public FileInfoProvider
{
  public:
    [[nodiscard]] std::vector<FileEntry> listDirectory(std::string const& path) const override;
};

} // namespace endo::platform

// Backward-compatible alias
namespace endo
{
using endo::platform::LinuxFileInfoProvider;
} // namespace endo
