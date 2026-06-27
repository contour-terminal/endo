// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>
#include <system_error>

namespace endo::platform
{

/// Parses the UID from a /proc/[pid]/status "Uid:" line, e.g. "Uid:\t1000\t1000\t1000\t1000".
/// Returns the first (real) UID, or std::nullopt on parse failure.
inline std::optional<int> parseUidFromStatusLine(std::string_view line)
{
    constexpr auto Prefix = std::string_view("Uid:");
    if (!line.starts_with(Prefix))
        return std::nullopt;
    line.remove_prefix(Prefix.size());
    while (!line.empty() && (line.front() == '\t' || line.front() == ' '))
        line.remove_prefix(1);
    if (line.empty())
        return std::nullopt;
    int uid = 0;
    if (auto [ptr, ec] = std::from_chars(line.data(), line.data() + line.size(), uid); ec == std::errc {})
        return uid;
    return std::nullopt;
}

/// Parses VmRSS in kB from a /proc/[pid]/status "VmRSS:" line, e.g. "VmRSS:\t  12345 kB".
/// Returns the memory value in kilobytes, or std::nullopt on parse failure.
inline std::optional<int64_t> parseVmRssFromStatusLine(std::string_view line)
{
    constexpr auto Prefix = std::string_view("VmRSS:");
    if (!line.starts_with(Prefix))
        return std::nullopt;
    line.remove_prefix(Prefix.size());
    while (!line.empty() && (line.front() == '\t' || line.front() == ' '))
        line.remove_prefix(1);
    if (line.empty())
        return std::nullopt;
    int64_t rss = 0;
    if (auto [ptr, ec] = std::from_chars(line.data(), line.data() + line.size(), rss); ec == std::errc {})
        return rss;
    return std::nullopt;
}

} // namespace endo::platform
