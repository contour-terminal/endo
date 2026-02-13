// SPDX-License-Identifier: Apache-2.0
#include "LinuxFileInfoProvider.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>

namespace endo
{

std::vector<FileEntry> LinuxFileInfoProvider::listDirectory(std::string const& path) const
{
    std::vector<FileEntry> entries;
    std::error_code ec;

    for (auto const& dirEntry: std::filesystem::directory_iterator(path, ec))
    {
        if (ec)
            break;

        FileEntry entry {};
        entry.name = dirEntry.path().filename().string();

        auto const status = dirEntry.status(ec);
        if (ec)
        {
            ec.clear();
            continue;
        }

        entry.isDir = std::filesystem::is_directory(status);
        entry.mode = static_cast<int64_t>(status.permissions()) & 0777;

        entry.size = 0;
        if (!entry.isDir)
        {
            auto const fileSize = dirEntry.file_size(ec);
            if (!ec)
                entry.size = static_cast<int64_t>(fileSize);
            else
                ec.clear();
        }

        auto const lwt = dirEntry.last_write_time(ec);
        if (!ec)
        {
            auto const sysTime = std::chrono::clock_cast<std::chrono::system_clock>(lwt);
            entry.mtime =
                std::chrono::duration_cast<std::chrono::seconds>(sysTime.time_since_epoch()).count();
        }
        else
        {
            ec.clear();
        }

        entries.push_back(std::move(entry));
    }

    // Sort by name for deterministic output
    std::ranges::sort(entries, {}, &FileEntry::name);
    return entries;
}

} // namespace endo
