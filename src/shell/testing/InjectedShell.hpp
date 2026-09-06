// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/Shell.hpp>
#include <shell/SixelCapability.hpp>
#include <shell/TTY.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include <platform/testing/InMemoryFileSystem.hpp>
#include <platform/testing/TestEnvironmentProvider.hpp>

namespace endo::testing
{

/// @brief Seeds the "/test" convention every injected-filesystem fixture shares.
///
/// Kept in one place because a fixture that seeds it differently is not a variant, it is a
/// bug: the shell resolves a relative path through the filesystem and the environment both,
/// so the two views of the working directory have to agree. $HOME matters for the same
/// reason -- it decides where per-user state such as the trust store lives, and pointing it
/// at the host's would put every test back on one shared real file.
///
/// @param fs    The injected filesystem to seed.
/// @param env   The injected environment to keep in step with it.
/// @param shell The shell owning them.
inline void seedInjectedShell(endo::InMemoryFileSystem& fs, endo::TestEnvironment& env, endo::Shell& shell)
{
    fs.addDirectory("/test");
    fs.setCurrentPath("/test");
    env.set("HOME", "/test/home");
    env.set("PWD", "/test");
    shell.addModuleSearchPath("/test");
    // Never probe the test PTY for Sixel support: the query would leak escape bytes into
    // the captured output and stall on timeout.
    shell.setSixelCapability(std::make_unique<endo::StaticSixelCapability>(false));
}

/// @brief A shell whose filesystem is private to the test case.
///
/// Nothing it reads or writes touches the host's disk, so concurrent instances cannot
/// collide and no fixture has to be cleaned up. Tests that need a real descriptor -- a
/// redirect target, an external program, process substitution -- cannot use this: a forked
/// child inherits descriptors, and there is no descriptor for an in-memory file. Those keep
/// using a real filesystem.
///
/// The in-memory model is also deliberately shallow in places: lastWriteTime() always reports
/// "now", weaklyCanonical() never resolves symlinks (the type queries and the stream opens do
/// follow them), and permissions are not enforced beyond denyAccess(). A test whose assertion
/// depends on any of those would pass vacuously here and belongs on a real filesystem.
struct InMemoryShell
{
    endo::TestPTY pty;
    endo::InMemoryFileSystem fs;
    endo::TestEnvironment env { "/test" };
    int exitCode = -1;

    endo::Shell shell { pty, env, fs };

    [[nodiscard]] std::string output() const { return pty.output(); }

    /// @param path Path within the injected filesystem.
    /// @return The file's content, or an empty string when it does not exist.
    [[nodiscard]] std::string content(std::filesystem::path const& path) const
    {
        return fs.readFile(path).value_or(std::string {});
    }

    InMemoryShell() { seedInjectedShell(fs, env, shell); }

    InMemoryShell& operator()(std::string_view cmd)
    {
        exitCode = shell.execute(std::string(cmd));
        return *this;
    }
};

} // namespace endo::testing
