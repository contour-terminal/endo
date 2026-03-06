// SPDX-License-Identifier: Apache-2.0
#include <shell/Shell.hpp>
#include <shell/util/GlobMatcher.hpp>
#include <shell/util/Suggestions.hpp>

#include <CoreVM/CoreVM.hpp>

#include <format>

namespace endo
{

using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;

void Shell::builtinForInit(CoreVM::Params& context)
{
    auto const& varName = context.getString(1);
    _forLoopStack.emplace_back();
    _forLoopStack.back().variable = varName;
    _forLoopStack.back().index = 0;
}

void Shell::builtinForAddItem(CoreVM::Params& context)
{
    if (_forLoopStack.empty())
        return;
    auto const& item = context.getString(1);
    _forLoopStack.back().items.push_back(item);
}

void Shell::builtinForHasMore(CoreVM::Params& context)
{
    if (_forLoopStack.empty())
    {
        context.setResult(false);
        return;
    }
    auto const& state = _forLoopStack.back();
    context.setResult(state.index < state.items.size());
}

void Shell::builtinForNext(CoreVM::Params& context)
{
    if (_forLoopStack.empty())
        return;

    auto& state = _forLoopStack.back();
    auto const& varName = context.getString(1);

    if (state.index < state.items.size())
    {
        _env.set(varName, state.items[state.index]);
        ++state.index;
    }
}

void Shell::builtinForCleanup([[maybe_unused]] CoreVM::Params& context)
{
    if (!_forLoopStack.empty())
        _forLoopStack.pop_back();
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void Shell::builtinCaseMatch(CoreVM::Params& context)
{
    auto const& word = context.getString(1);
    auto const& pattern = context.getString(2);

    bool const matched = globMatchFilename(word, pattern);
    context.setResult(matched);
}

void Shell::builtinFunctionRegister(CoreVM::Params& context)
{
    auto const& name = context.getString(1);
    _registeredFunctions.insert(name);
}

void Shell::builtinFunctionCall(CoreVM::Params& context)
{
    auto const& name = context.getString(1);

    if (!_registeredFunctions.contains(name))
    {
        auto candidates =
            std::vector<std::string_view>(_registeredFunctions.begin(), _registeredFunctions.end());
        auto const suggestions = SuggestionGenerator::suggestCommand(name, candidates);
        auto hint = suggestions.empty()
                        ? std::string {}
                        : std::format(" {}", SuggestionGenerator::formatDidYouMean(suggestions.front()));
        error("{}: command not found{}", name, hint);
        _exitCode = 127;
        context.setResult(CoreVM::CoreNumber(127));
        return;
    }

    CoreVM::Function* fn = _currentProgram->findFunction(name);
    if (!fn)
    {
        error("{}: function not found (was it defined in a previous command?)", name);
        _exitCode = 127;
        context.setResult(CoreVM::CoreNumber(127));
        return;
    }

    auto runner = CoreVM::Runner(fn,
                                 nullptr,
                                 &_globals,
                                 CoreVM::RuntimeConfig::defaultConfig(),
                                 std::bind(&Shell::trace, this, _1, _2, _3));
    runner.run();
    context.setResult(CoreVM::CoreNumber(_exitCode));
}

} // namespace endo
