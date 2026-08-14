// SPDX-License-Identifier: Apache-2.0
#include <array>
#include <format>
#include <ranges>

#include <platform/FileUri.hpp>

namespace endo::platform
{

namespace
{
    /// @brief Per-byte table of characters that need no percent-encoding in a URI path.
    ///
    /// The RFC 3986 unreserved set plus `/`. Built once at compile time so classification is
    /// a single indexed load and, unlike `std::isalnum`, cannot vary with the active locale.
    constexpr auto UriPathSafe = [] {
        auto table = std::array<bool, 256> {};
        for (auto const ch: std::views::iota('a', static_cast<char>('z' + 1)))
            table[static_cast<unsigned char>(ch)] = true;
        for (auto const ch: std::views::iota('A', static_cast<char>('Z' + 1)))
            table[static_cast<unsigned char>(ch)] = true;
        for (auto const ch: std::views::iota('0', static_cast<char>('9' + 1)))
            table[static_cast<unsigned char>(ch)] = true;
        for (auto const ch: std::string_view { "-._~/" })
            table[static_cast<unsigned char>(ch)] = true;
        return table;
    }();

    /// @brief Whether @p path starts with a Windows drive specifier such as `C:`.
    [[nodiscard]] constexpr bool hasDriveSpecifier(std::string_view path) noexcept
    {
        if (path.size() < 2 || path[1] != ':')
            return false;
        auto const drive = path[0];
        return (drive >= 'A' && drive <= 'Z') || (drive >= 'a' && drive <= 'z');
    }
} // namespace

auto percentEncodeUriPath(std::string_view path) -> std::string
{
    auto encoded = std::string {};
    encoded.reserve(path.size());
    for (auto const ch: path)
    {
        if (UriPathSafe[static_cast<unsigned char>(ch)])
            encoded += ch;
        else
            encoded += std::format("%{:02X}", static_cast<unsigned char>(ch));
    }
    return encoded;
}

auto fileUri(std::string_view absolutePath, std::string_view host, std::string_view fragment) -> std::string
{
    if (absolutePath.empty())
        return {};

    auto authority = std::string { host };
    auto path = absolutePath;

    if (path.starts_with("//"))
    {
        // UNC path: `//server/share/x`. RFC 8089 puts the server in the authority, so the
        // local hostname is irrelevant here — the file does not live on this machine.
        path.remove_prefix(2);
        auto const slash = path.find('/');
        authority = std::string { path.substr(0, slash) };
        path = slash == std::string_view::npos ? std::string_view {} : path.substr(slash);
    }

    auto uri = std::format("file://{}", authority);

    if (hasDriveSpecifier(path))
    {
        // A drive-letter path has no leading slash of its own, but the URI scheme requires
        // the path component to start with one: `C:/x` -> `/C:/x`. The drive specifier is
        // emitted verbatim so the colon survives — it is a valid pchar and is the form
        // terminals and `wslpath` expect. Colons elsewhere are still encoded.
        uri += '/';
        uri += path.substr(0, 2);
        path.remove_prefix(2);
    }

    uri += percentEncodeUriPath(path);

    if (!fragment.empty())
    {
        uri += '#';
        uri += percentEncodeUriPath(fragment);
    }

    return uri;
}

} // namespace endo::platform
