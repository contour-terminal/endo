// SPDX-License-Identifier: Apache-2.0
#include "WindowsFileInfoProvider.hpp"

#if defined(_WIN32)

    #include <chrono>
    #include <filesystem>
    #include <string>
    #include <vector>

namespace endo
{

std::vector<FileEntry> WindowsFileInfoProvider::listDirectory(std::string const& path) const
{
    std::vector<FileEntry> result;
    std::error_code ec;

    auto const dir = std::filesystem::path(path);
    if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec))
        return result;

    for (auto const& entry: std::filesystem::directory_iterator(dir, ec))
    {
        if (ec)
            break;

        FileEntry fileEntry {};
        fileEntry.name = entry.path().filename().string();
        fileEntry.isDir = entry.is_directory(ec);

        if (!fileEntry.isDir)
            fileEntry.size = static_cast<int64_t>(entry.file_size(ec));

        // Permissions: Windows only has read-only, so approximate
        auto const status = entry.status(ec);
        if (!ec)
        {
            auto const perms = status.permissions();
            int mode = 0;
            if ((perms & std::filesystem::perms::owner_read) != std::filesystem::perms::none)
                mode |= 0444;
            if ((perms & std::filesystem::perms::owner_write) != std::filesystem::perms::none)
                mode |= 0222;
            if ((perms & std::filesystem::perms::owner_exec) != std::filesystem::perms::none)
                mode |= 0111;
            if (fileEntry.isDir)
                mode |= 0111; // Directories are always "executable"
            fileEntry.mode = mode;
        }

        // Modification time
        auto const lastWrite = entry.last_write_time(ec);
        if (!ec)
        {
            auto const sctp = std::chrono::clock_cast<std::chrono::system_clock>(lastWrite);
            fileEntry.mtime = std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count();
        }

        result.push_back(std::move(fileEntry));
    }

    return result;
}

} // namespace endo

#endif // _WIN32
