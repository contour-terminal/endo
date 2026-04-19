// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <platform/EnvironmentProvider.hpp>

namespace endo
{

/// Returns the user's home directory as a forward-slash–normalized string.
///
/// Uses @c EnvironmentProvider::homeDirectory() (HOME → USERPROFILE fallback),
/// so it works on Windows when HOME is unset. The returned string uses
/// forward slashes regardless of platform, matching the convention used by
/// @c EnvironmentProvider::currentDirectory() and the rest of the shell.
/// Returns an empty string if neither variable is set.
[[nodiscard]] std::string normalizedHomeDirectory(EnvironmentProvider const& env);

/// Maximum number of required paths to record per history entry.
///
/// Caps cost of per-entry existence checks during completion and keeps
/// storage compact for commands that expand into many path-like args.
inline constexpr size_t maxRequiredPaths = 8;

/// Canonicalizes an absolute path into a home-relative form for portable storage.
///
/// If @p absPath is inside @p home, returns a `~/...` relative form; the bare
/// home path itself becomes `"~"`. Otherwise returns @p absPath unchanged.
/// Plain string comparison — no filesystem I/O. Uses component-aware prefix
/// matching so `/home/userx` is not treated as a child of `/home/user`.
///
/// @param absPath An absolute path.
/// @param home    The user's home directory (absolute path).
/// @return Canonical, home-relative form when applicable, otherwise @p absPath.
[[nodiscard]] std::string canonicalizeForHistory(std::string_view absPath, std::string_view home);

/// Expands a canonical (possibly `~`-prefixed) path back to an absolute path.
///
/// Inverse of canonicalizeForHistory(). Only handles leading `~/` and bare `~`
/// — does not expand `~user` forms. Other inputs are returned unchanged.
///
/// @param storedPath Path as stored in history.
/// @param home       The user's home directory (absolute path).
/// @return Expanded absolute path.
[[nodiscard]] std::string expandForLookup(std::string_view storedPath, std::string_view home);

/// Collects path-like arguments from a post-expansion argv.
///
/// Scans @p argv starting at index 1 (skipping the command name). For each
/// argument that looks like a path, resolves it to an absolute form (expanding
/// `~`, joining with @p cwdAbs if relative) and then canonicalizes it for
/// portable storage via canonicalizeForHistory().
///
/// An argument is considered path-like if it starts with `/`, `./`, `../`,
/// `~/`, or `~`, or if it contains a `/` character. Bare identifiers and
/// option flags (`-f`, `--flag`, `--foo=bar`) are skipped.
///
/// The returned list is capped at maxRequiredPaths entries.
///
/// @param argv    Post-expansion argument vector (argv[0] is the command).
/// @param cwdAbs  Absolute current working directory at command execution time.
/// @param home    The user's home directory (absolute path).
/// @return Canonical, portable paths referenced by the command.
[[nodiscard]] std::vector<std::string> collectRequiredPaths(std::span<std::string const> argv,
                                                            std::string_view cwdAbs,
                                                            std::string_view home);

/// Convenience overload that tokenizes a raw command line before collecting paths.
///
/// Uses a lightweight shell-aware splitter: whitespace separates tokens, single
/// and double quotes group a token (quotes are stripped from the result). Does
/// NOT perform variable expansion, pipelines, redirection, or glob expansion —
/// the goal is best-effort path detection, not faithful shell parsing. Good
/// enough for real interactive lines like `vim ~/notes/plan.md` or
/// `cp "/tmp/a b.txt" /home/u/b`.
///
/// @param commandLine The raw command line typed by the user.
/// @param cwdAbs      Absolute current working directory at command execution time.
/// @param home        The user's home directory (absolute path).
/// @return Canonical, portable paths referenced by the command.
[[nodiscard]] std::vector<std::string> collectRequiredPathsFromCommandLine(std::string_view commandLine,
                                                                           std::string_view cwdAbs,
                                                                           std::string_view home);

} // namespace endo
