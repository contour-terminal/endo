// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <map>
#include <string>
#include <utility>

#include <platform/FileInfoProvider.hpp>

namespace endo::platform::testing
{

/// Mock FileInfoProvider for unit testing.
///
/// Returns configurable directory listings keyed by path.
class MockFileInfoProvider final: public FileInfoProvider
{
  public:
    /// Sets the entries to return for a given path.
    void setEntries(std::string const& path, std::vector<FileEntry> entries)
    {
        _directories[path] = std::move(entries);
    }

    [[nodiscard]] std::vector<FileEntry> listDirectory(std::string const& path) const override
    {
        if (auto const it = _directories.find(path); it != _directories.end())
            return it->second;
        return {};
    }

  private:
    std::map<std::string, std::vector<FileEntry>> _directories;
};

} // namespace endo::platform::testing
