// SPDX-License-Identifier: Apache-2.0
#include <array>
#include <format>
#include <ranges>

#include <platform/FileUri.hpp>

namespace endo::platform
{

namespace
{
    /// @brief Bytes that need no percent-encoding in a URI path.
    ///
    /// The RFC 3986 unreserved set plus `/`, spelled once as data. Built at compile time so
    /// classification is a single indexed load and, unlike a ctype call, cannot vary with locale.
    constexpr auto UriPathSafe = [] {
        auto table = std::array<bool, 256> {};
        for (auto const ch:
             std::string_view { "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~/" })
            table[static_cast<unsigned char>(ch)] = true;
        return table;
    }();

    constexpr auto HexDigits = std::string_view { "0123456789ABCDEF" };

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
        auto const byte = static_cast<unsigned char>(ch);
        if (UriPathSafe[byte])
        {
            encoded += ch;
        }
        else
        {
            encoded += '%';
            encoded += HexDigits[byte >> 4U];
            encoded += HexDigits[byte & 0x0FU];
        }
    }
    return encoded;
}

auto fileUri(std::string_view absolutePath, std::string_view host, std::string_view fragment) -> std::string
{
    if (absolutePath.empty())
        return {};

    auto authority = host;
    auto path = absolutePath;

    if (path.starts_with("//"))
    {
        // UNC path: `//server/share/x`. RFC 8089 puts the server in the authority, so the
        // local hostname is irrelevant here — the file does not live on this machine.
        path.remove_prefix(2);
        auto const slash = path.find('/');
        authority = path.substr(0, slash);
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
