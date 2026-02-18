// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <platform/FileInfoProvider.hpp>

namespace endo::platform
{

/// Windows implementation of FileInfoProvider using std::filesystem.
///
/// Nearly identical to LinuxFileInfoProvider since both use std::filesystem.
/// The only difference is that Windows permissions are limited (read-only flag only,
/// no owner/group/others distinction).
class WindowsFileInfoProvider final: public FileInfoProvider
{
  public:
    [[nodiscard]] std::vector<FileEntry> listDirectory(std::string const& path) const override;
};

} // namespace endo::platform

// Backward-compatible alias
namespace endo
{
using endo::platform::WindowsFileInfoProvider;
} // namespace endo
