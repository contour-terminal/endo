// SPDX-License-Identifier: Apache-2.0
#include <shell/Shell.hpp>
#include <shell/util/GlobMatcher.hpp>

#include <charconv>
#include <cmath>
#include <filesystem>

#include <platform/Types.hpp>

#if !defined(_WIN32)
    #include <pwd.h>
#endif

namespace endo
{

void Shell::builtinExpandTilde(CoreVM::Params& context)
{
    auto const& suffix = context.getString(1);
    std::string home = std::string(_env.get("HOME").value_or(""));
    context.setResult(home + suffix);
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void Shell::builtinExpandTildeUser(CoreVM::Params& context)
{
    auto const& user = context.getString(1);
    auto const& suffix = context.getString(2);
#if !defined(_WIN32)
    if (auto* pw = getpwnam(user.c_str()); pw != nullptr)
        context.setResult(std::string(pw->pw_dir) + suffix);
    else
        context.setResult("~" + user + suffix);
#else
    // Heuristic: derive the users base directory from USERPROFILE and append the requested username.
    if (auto const userProfile = _env.get("USERPROFILE"); userProfile.has_value())
    {
        auto const usersDir = std::filesystem::path(*userProfile).parent_path();
        auto const targetHome = usersDir / user;
        if (std::filesystem::exists(targetHome))
        {
            context.setResult(targetHome.string() + suffix);
            return;
        }
    }
    // Fallback: return the literal ~username string when expansion fails.
    context.setResult("~" + user + suffix);
#endif
}

void Shell::builtinExpandGlob(CoreVM::Params& context)
{
    auto const& pattern = context.getString(1);

    auto matches = expandGlobPattern(pattern);
    if (matches.empty())
    {
        cmdBuilderArgs().push_back(pattern);
    }
    else
    {
        for (auto& match: matches)
            cmdBuilderArgs().push_back(std::move(match));
    }
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void Shell::builtinArithToString(CoreVM::Params& context)
{
    auto const unsignedValue = context.getInt(1);
    auto const signedValue = static_cast<int64_t>(unsignedValue);
    context.setResult(std::to_string(signedValue));
}

void Shell::builtinArithGetVar(CoreVM::Params& context)
{
    auto const& name = context.getString(1);
    auto const value = _env.get(name);
    if (!value.has_value() || value->empty())
    {
        context.setResult(CoreVM::CoreNumber(0));
        return;
    }
    int64_t result = 0;
    // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
    auto [ptr, ec] = std::from_chars(value->data(), value->data() + value->size(), result);
    context.setResult(CoreVM::CoreNumber(result));
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void Shell::builtinArithPow(CoreVM::Params& context)
{
    auto const base = context.getInt(1);
    auto const exp = context.getInt(2);
    if (exp < 0)
    {
        context.setResult(CoreVM::CoreNumber(0));
        return;
    }
    int64_t result = 1;
    for (int64_t i = 0; i < exp; ++i)
        result *= base;
    context.setResult(CoreVM::CoreNumber(result));
}

void Shell::builtinExpandParamLength(CoreVM::Params& context)
{
    auto const& varName = context.getString(1);
    auto const value = _env.get(varName);
    context.setResult(std::to_string(value.value_or("").size()));
}

void Shell::builtinExpandParamDefault(CoreVM::Params& context)
{
    auto const& varName = context.getString(1);
    auto const& defaultValue = context.getString(2);
    auto const value = _env.get(varName);
    if (value.has_value() && !value->empty())
        context.setResult(std::string(*value));
    else
        context.setResult(defaultValue);
}

void Shell::builtinExpandParamAlternate(CoreVM::Params& context)
{
    auto const& varName = context.getString(1);
    auto const& alternate = context.getString(2);
    auto const value = _env.get(varName);
    if (value.has_value() && !value->empty())
        context.setResult(alternate);
    else
        context.setResult("");
}

void Shell::builtinExpandParamAssign(CoreVM::Params& context)
{
    auto const& varName = context.getString(1);
    auto const& defaultValue = context.getString(2);
    auto const value = _env.get(varName);
    if (value.has_value() && !value->empty())
        context.setResult(std::string(*value));
    else
    {
        _env.set(varName, defaultValue);
        context.setResult(defaultValue);
    }
}

void Shell::builtinExpandParamError(CoreVM::Params& context)
{
    auto const& varName = context.getString(1);
    auto const& errorMsg = context.getString(2);
    auto const value = _env.get(varName);
    if (value.has_value() && !value->empty())
        context.setResult(std::string(*value));
    else
    {
        std::string const msg = errorMsg.empty() ? std::format("{}: parameter null or not set", varName)
                                                 : std::format("{}: {}", varName, errorMsg);
        error("{}", msg);
        _exitCode = 1;
        context.setResult("");
    }
}

void Shell::builtinExpandParamRemovePrefix(CoreVM::Params& context)
{
    auto const& varName = context.getString(1);
    auto const& pattern = context.getString(2);
    bool const longest = context.getBool(3);
    auto const value = _env.get(varName);
    std::string const val = std::string(value.value_or(""));

    if (val.empty() || pattern.empty())
    {
        context.setResult(val);
        return;
    }

    auto const matches = findPrefixMatches(val, pattern);
    if (matches.empty())
    {
        context.setResult(val);
        return;
    }

    size_t const matchLen = longest ? matches.back() : matches.front();
    context.setResult(val.substr(matchLen));
}

void Shell::builtinExpandParamRemoveSuffix(CoreVM::Params& context)
{
    auto const& varName = context.getString(1);
    auto const& pattern = context.getString(2);
    bool const longest = context.getBool(3);
    auto const value = _env.get(varName);
    std::string const val = std::string(value.value_or(""));

    if (val.empty() || pattern.empty())
    {
        context.setResult(val);
        return;
    }

    auto const matches = findSuffixMatches(val, pattern);
    if (matches.empty())
    {
        context.setResult(val);
        return;
    }

    size_t const matchStart = longest ? matches.front() : matches.back();
    context.setResult(val.substr(0, matchStart));
}

void Shell::builtinExpandParamReplace(CoreVM::Params& context)
{
    auto const& varName = context.getString(1);
    auto const& pattern = context.getString(2);
    auto const& replacement = context.getString(3);
    bool const replaceAll = context.getBool(4);
    auto const value = _env.get(varName);
    std::string const val = std::string(value.value_or(""));

    if (val.empty() || pattern.empty())
    {
        context.setResult(val);
        return;
    }

    std::string result;
    size_t pos = 0;

    while (pos < val.size())
    {
        auto const matchResult = findPatternMatchLength(std::string_view(val).substr(pos), pattern);
        if (matchResult.has_value())
        {
            result += replacement;
            pos += *matchResult;
            if (!replaceAll)
            {
                result += val.substr(pos);
                break;
            }
        }
        else
        {
            result += val[pos];
            ++pos;
        }
    }

    context.setResult(result);
}

} // namespace endo
