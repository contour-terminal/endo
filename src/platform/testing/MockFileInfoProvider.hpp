// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <map>
#include <string>
#include <utility>

#include <platform/FileInfoProvider.hpp>
#include <platform/GlobMatch.hpp>

namespace endo::platform::testing
{

/// Mock FileInfoProvider for unit testing.
///
/// Returns configurable directory listings keyed by path.
/// Supports single-file lookups and basic glob pattern filtering.
class MockFileInfoProvider final: public FileInfoProvider
{
  public:
    /// Sets the entries to return for a given directory path.
    void setEntries(std::string const& path, std::vector<FileEntry> entries)
    {
        _directories[path] = std::move(entries);
    }

    /// Sets a single file entry to return for a given file path.
    void setFileEntry(std::string const& path, FileEntry entry) { _files[path] = std::move(entry); }

    [[nodiscard]] std::vector<FileEntry> listDirectory(std::string const& path) const override
    {
        // Case 1: Exact directory match.
        if (auto const it = _directories.find(path); it != _directories.end())
            return it->second;

        // Case 2: Exact single-file match.
        if (auto const it = _files.find(path); it != _files.end())
            return { it->second };

        // Case 3: Glob pattern — find parent directory entries and filter.
        if (endo::containsGlobChars(path))
        {
            // Extract parent directory and filename pattern.
            auto const lastSlash = path.find_last_of('/');
            auto const parentDir =
                (lastSlash != std::string::npos) ? path.substr(0, lastSlash) : std::string(".");
            auto const pattern = (lastSlash != std::string::npos) ? path.substr(lastSlash + 1) : path;

            if (auto const dirIt = _directories.find(parentDir); dirIt != _directories.end())
            {
                std::vector<FileEntry> result;
                for (auto const& entry: dirIt->second)
                {
                    if (endo::globMatchFilename(entry.name, pattern))
                        result.push_back(entry);
                }
                return result;
            }
        }

        return {};
    }

  private:
    std::map<std::string, std::vector<FileEntry>> _directories;
    std::map<std::string, FileEntry> _files;
};

} // namespace endo::platform::testing
