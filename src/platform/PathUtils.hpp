// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>

namespace endo::platform
{

/// @brief Compares two strings for equality, ignoring ASCII lettercase.
///
/// @param a First string.
/// @param b Second string.
/// @return True if @p a and @p b are equal when compared case-insensitively.
[[nodiscard]] inline bool equalsCaseInsensitive(std::string_view a, std::string_view b) noexcept
{
    return std::ranges::equal(
        a, b, [](unsigned char x, unsigned char y) noexcept { return std::tolower(x) == std::tolower(y); });
}

/// @brief Whether the host filesystem resolves paths case-insensitively by default.
///
/// True on Windows and the default macOS volume format, where `foo` and `FOO` name the
/// same entry. Path completion and matching consult this so the shell mirrors how the
/// OS itself resolves names — e.g. completing `Lastrada-to` to the on-disk
/// `lastrada-tools/`. POSIX (Linux) filesystems are case-sensitive, so it is false there
/// and smart-case matching is retained.
inline constexpr bool FilesystemCaseInsensitive =
#if defined(_WIN32) || defined(__APPLE__)
    true;
#else
    false;
#endif

/// @brief Normalizes a path string to use forward slashes.
///
/// On Windows, replaces all backslashes with forward slashes.
/// Preserves UNC paths by normalizing `\\server\share` to `//server/share`.
/// No-op on POSIX platforms.
///
/// @param path The path string to normalize
/// @return The normalized path string
[[nodiscard]] inline auto normalizePath(std::string path) -> std::string
{
#if defined(_WIN32)
    std::ranges::replace(path, '\\', '/');
#endif
    return path;
}

/// @brief Normalizes a filesystem path to use forward slashes.
///
/// Convenience overload that converts a std::filesystem::path to a normalized string.
///
/// @param p The filesystem path to normalize
/// @return The normalized path string
[[nodiscard]] inline auto normalizePath(std::filesystem::path const& p) -> std::string
{
    return p.generic_string();
}

/// @brief Resolves @p path against @p base, without touching the filesystem.
///
/// A path that is already absolute is returned normalized and otherwise untouched. A relative
/// one is joined onto @p base and lexically normalized, so `./x` and `a/../b` collapse. Purely
/// lexical by design: callers resolving thousands of entries cannot afford a syscall each, and
/// symlinks must not be followed.
///
/// @param path The path to resolve.
/// @param base Absolute directory to resolve against. Empty returns @p path normalized only,
///             since there is nothing to resolve against.
/// @return The resolved, forward-slash normalized path.
[[nodiscard]] inline auto absolutePath(std::string_view path, std::string_view base) -> std::string
{
    auto const candidate = std::filesystem::path { path };
    if (base.empty() || candidate.is_absolute())
        return normalizePath(candidate.lexically_normal());

    return normalizePath((std::filesystem::path { base } / candidate).lexically_normal());
}

/// @brief Returns a path with its real on-disk capitalization and forward slashes.
///
/// On Windows the working directory and user-typed paths preserve whatever case was
/// entered (e.g. `GetCurrentDirectoryW` echoes the case passed to `SetCurrentDirectory`),
/// which differs from how the path is actually stored on disk. This helper resolves the
/// path to its canonical, correctly-cased form (via `GetFinalPathNameByHandleW`) and
/// upper-cases a leading drive letter, then normalizes separators to forward slashes.
/// Unlike normalizePath() this is a filesystem-touching operation — the path must exist
/// to be recased; if it does not exist or the lookup fails, it falls back to a plain
/// normalizePath().
///
/// On POSIX this is equivalent to normalizePath(): the filesystem is case-sensitive, so a
/// path's spelling is already its canonical case.
///
/// @param p The path to canonicalize.
/// @return The path with on-disk capitalization and forward slashes.
[[nodiscard]] auto canonicalCasePath(std::filesystem::path const& p) -> std::string;

/// @brief Tests whether two paths name the same directory entry differing only in
/// the lettercase of their final component.
///
/// Returns true when @p from and @p to share an identical parent path and have final
/// components that compare equal case-insensitively (ASCII) yet differ byte-for-byte —
/// e.g. `foo` vs `Foo`, or `dir/a` vs `dir/A`. This is the signature of a
/// rename-to-recase, which case-insensitive filesystems (Windows, the default macOS
/// volume format) cannot perform with a single rename and which `mv` must route as an
/// in-place rename rather than a move-into-directory. Returns false when the final
/// components are byte-identical (a true no-op rename), when either has no final
/// component, or when the parents differ.
///
/// Both paths are lexically normalized and stripped of a trailing separator before
/// comparison; the check is purely lexical and touches no filesystem.
///
/// @param from The source path.
/// @param to The destination path.
/// @return True if the rename only changes the lettercase of the final component.
[[nodiscard]] bool isCaseOnlyRename(std::filesystem::path const& from, std::filesystem::path const& to);

/// @brief Resolves POSIX-style device paths to their platform-native equivalent.
///
/// Endo accepts POSIX device paths (`/dev/null`) across all platforms for portability.
/// On Windows these paths must be rewritten to the corresponding reserved device name
/// before being passed to file-open APIs:
///
/// | POSIX      | Windows |
/// |------------|---------|
/// | `/dev/null` | `NUL`   |
///
/// On POSIX platforms this helper is a no-op — device paths are native and require no
/// translation.
///
/// Apply this to **user-supplied** paths at the point they are opened (e.g. before
/// `std::ofstream`, `std::ifstream`, or low-level `open`/`CreateFileW` calls). Do not
/// apply it to generated or internal paths, as the mapping is intentionally limited to
/// the device names that users type.
///
/// @param path The path to resolve.
/// @return The platform-native path.
[[nodiscard]] inline auto resolveDevicePath(std::string_view path) -> std::string
{
#if defined(_WIN32)
    if (path == "/dev/null")
        return "NUL";
#endif
    return std::string { path };
}

} // namespace endo::platform
