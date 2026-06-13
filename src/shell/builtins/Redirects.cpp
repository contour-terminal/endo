// SPDX-License-Identifier: Apache-2.0
#include <shell/Shell.hpp>

#include <fcntl.h>

#include <platform/Process.hpp>
#include <platform/Types.hpp>

namespace endo
{

void Shell::builtinOpenRead(CoreVM::Params& context)
{
    auto const& path = context.getString(1);
    auto const result = _processManager.openFile(path, O_RDONLY);
    if (result.has_value())
    {
        context.setResult(static_cast<CoreVM::CoreNumber>(result.value()));
    }
    else
    {
        error("Failed to open '{}' for reading: {}", path, toString(result.error()));
        context.setResult(static_cast<CoreVM::CoreNumber>(-1));
    }
}

void Shell::builtinOpenWrite(CoreVM::Params& context)
{
    auto const& path = context.getString(1);
    int const oflags = static_cast<int>(context.getInt(2));
    auto const result = _processManager.openFile(path, oflags);
    if (result.has_value())
    {
        context.setResult(static_cast<CoreVM::CoreNumber>(result.value()));
    }
    else
    {
        error("Failed to open '{}' for writing: {}", path, toString(result.error()));
        context.setResult(static_cast<CoreVM::CoreNumber>(-1));
    }
}

void Shell::builtinRedirectStart(CoreVM::Params&)
{
    _redirectState.clear();
}

void Shell::builtinRedirectInput(CoreVM::Params& context)
{
    int const targetFd = static_cast<int>(context.getInt(1));
    std::string path = context.getString(2);
    _redirectState.addInputFile(targetFd, std::move(path));
}

void Shell::builtinRedirectOutput(CoreVM::Params& context)
{
    int const sourceFd = static_cast<int>(context.getInt(1));
    std::string path = context.getString(2);
    bool const append = context.getBool(3);
    _redirectState.addOutputFile(sourceFd, std::move(path), append);
}

void Shell::builtinRedirectFdDup(CoreVM::Params& context)
{
    int const sourceFd = static_cast<int>(context.getInt(1));
    int const targetFd = static_cast<int>(context.getInt(2));
    _redirectState.addFdDup(sourceFd, targetFd);
}

void Shell::builtinRedirectHeredoc(CoreVM::Params& context)
{
    int const targetFd = static_cast<int>(context.getInt(1));
    std::string content = context.getString(2);
    _redirectState.addHereDoc(targetFd, std::move(content));
}

void Shell::builtinRedirectHerestring(CoreVM::Params& context)
{
    int const targetFd = static_cast<int>(context.getInt(1));
    std::string content = context.getString(2);
    _redirectState.addHereString(targetFd, std::move(content));
}

void Shell::builtinRedirectEnd(CoreVM::Params&)
{
    for (auto& entry: _redirectState.entries)
    {
        if (entry.openedFd != InvalidHandle && entry.openedFd != standardInput()
            && entry.openedFd != standardOutput() && entry.openedFd != standardError())
        {
            _processManager.closeHandle(entry.openedFd);
            entry.openedFd = InvalidHandle;
        }
    }
    _redirectState.clear();
}

} // namespace endo
