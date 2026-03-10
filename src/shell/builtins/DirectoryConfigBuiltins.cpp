// SPDX-License-Identifier: Apache-2.0
#include <shell/DirectoryConfig.hpp>
#include <shell/Shell.hpp>
#include <shell/TTY.hpp>

#include <filesystem>
#include <format>
#include <string>

#include <platform/Types.hpp>

namespace endo
{

void Shell::registerDirectoryConfigBuiltins()
{
    // dirconfig is handled as an inline builtin (via executeInlineDirConfig),
    // dispatched from tryExecuteInlineBuiltin, not a runtime-registered function.
}

int Shell::executeInlineDirConfig(CoreVM::CoreStringArray const& args, NativeHandle outputFd)
{
    auto const writeOutput = [outputFd](std::string const& msg) {
        [[maybe_unused]] auto const written = platformWrite(outputFd, msg.data(), msg.size());
    };

    if (args.size() < 2)
    {
        writeOutput("Usage: dirconfig <allow|deny|list|revoke|reload> [path]\n");
        return 1;
    }

    auto const& subcmd = args.at(1);

    if (subcmd == "list")
    {
        auto const& entries = _dirConfigManager->trustEntries();
        if (entries.empty())
        {
            writeOutput("No directory configs registered.\n");
        }
        else
        {
            for (auto const& [path, entry]: entries)
            {
                auto const* const status = entry.allowed ? "allowed" : "denied";
                writeOutput(std::format("{} [{}]\n", path, status));
            }
        }
        return 0;
    }

    if (subcmd == "reload")
    {
        _dirConfigManager->reloadConfigs();
        return 0;
    }

    // Resolve path: use arg[2] if given, else CWD
    auto const configPath = [&]() -> std::filesystem::path {
        if (args.size() >= 3)
            return std::filesystem::path(args.at(2));
        return std::filesystem::path(_env.currentDirectory()) / ".local-env.endo";
    }();

    if (subcmd == "allow")
    {
        _dirConfigManager->allowConfig(configPath);
        return 0;
    }
    if (subcmd == "deny")
    {
        _dirConfigManager->denyConfig(configPath);
        return 0;
    }
    if (subcmd == "revoke")
    {
        _dirConfigManager->revokeConfig(configPath);
        return 0;
    }

    _tty.writeToStderr(
        std::format("dirconfig: unknown subcommand '{}' (use: allow, deny, list, revoke, reload)\n", subcmd));
    return 1;
}

} // namespace endo
