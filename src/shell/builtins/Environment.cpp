// SPDX-License-Identifier: Apache-2.0
#include <shell/Shell.hpp>

#include <filesystem>
#include <format>

#include <platform/Types.hpp>

namespace endo
{

void Shell::builtinExit(CoreVM::Params& context)
{
    _exitCode = static_cast<int>(context.getInt(1));
    _runner->suspend();
    _quit = true;
}

void Shell::builtinChDir(CoreVM::Params& context)
{
    auto path = std::string(context.getString(1));

    if (path == "-")
    {
        auto const oldpwd = _env.get("OLDPWD");
        if (!oldpwd.has_value() || oldpwd->empty())
        {
            error("cd: OLDPWD not set");
            _exitCode = 1;
            context.setResult(false);
            return;
        }
        path = std::string(*oldpwd);
    }

    auto const result = _env.changeDirectory(path);
    if (!result.has_value())
    {
        error("Failed to change directory to '{}': {}", path, toString(result.error()));
        _exitCode = 1;
    }
    else
    {
        _env.set("OLDPWD", _env.get("PWD").value_or(""));
        _env.set("PWD", _env.currentDirectory());
        _exitCode = 0;
        emitCurrentWorkingDirectory();
    }

    context.setResult(result.has_value());
}

void Shell::builtinChDirHome(CoreVM::Params& context)
{
    auto const path = _env.get("HOME").value_or("/");

    auto const result = _env.changeDirectory(std::filesystem::path(path));
    if (!result.has_value())
    {
        error("Failed to change directory to '{}': {}", path, toString(result.error()));
        _exitCode = 1;
    }
    else
    {
        _env.set("OLDPWD", _env.get("PWD").value_or(""));
        _env.set("PWD", _env.currentDirectory());
        _exitCode = 0;
        emitCurrentWorkingDirectory();
    }

    context.setResult(result.has_value());
}

void Shell::builtinSet(CoreVM::Params& context)
{
    _env.set(context.getString(1), context.getString(2));
    context.setResult(true);
}

void Shell::builtinUnset(CoreVM::Params& context)
{
    _env.unset(context.getString(1));
    context.setResult(true);
}

void Shell::builtinGetVar(CoreVM::Params& context)
{
    auto const& name = context.getString(1);
    auto const value = _env.get(name);
    context.setResult(std::string(value.value_or("")));
}

void Shell::builtinGetExitStatus(CoreVM::Params& context)
{
    context.setResult(std::to_string(_exitCode));
}

void Shell::builtinSetExitStatus(CoreVM::Params& context)
{
    _exitCode = static_cast<int>(context.getInt(1));
}

void Shell::builtinGetProcessId(CoreVM::Params& context)
{
    context.setResult(std::to_string(_shellPid));
}

void Shell::builtinGetBackgroundId(CoreVM::Params& context)
{
    if (_lastBackgroundPid.has_value())
        context.setResult(std::to_string(_lastBackgroundPid.value()));
    else
        context.setResult("");
}

void Shell::builtinGetPositional(CoreVM::Params& context)
{
    auto const index = static_cast<size_t>(context.getInt(1));
    if (index < _positionalParameters.size())
        context.setResult(_positionalParameters[index]);
    else
        context.setResult("");
}

void Shell::builtinSetAndExport(CoreVM::Params& context)
{
    _env.set(context.getString(1), context.getString(2));
    _env.exportVariable(context.getString(1));
}

void Shell::builtinExport(CoreVM::Params& context)
{
    _env.exportVariable(context.getString(1));
}

} // namespace endo
