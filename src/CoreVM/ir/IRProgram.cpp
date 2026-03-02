// SPDX-License-Identifier: Apache-2.0
#include <CoreVM/CoreVM.hpp>

#include <algorithm>
#include <cassert>
#include <iostream>
#include <memory>
#include <sstream>

namespace CoreVM
{

IRProgram::IRProgram(): _trueLiteral(true, "trueLiteral"), _falseLiteral(false, "falseLiteral")
{
}

#define GLOBAL_SCOPE_INIT_NAME "@__global_init__"

IRProgram::~IRProgram()
{
    // first reset all standard functions and *then* the global-scope initialization function
    // in order to not cause confusion upon resource release
    {
        std::unique_ptr<IRFunction> global;
        auto gh = std::ranges::find_if(
            _functions, [](auto& function) { return function->name() == GLOBAL_SCOPE_INIT_NAME; });
        if (gh != _functions.end())
        {
            global = std::move(*gh);
            _functions.erase(gh);
        }
        _functions.clear();
        global.reset(nullptr);
    }

    _constantArrays.clear();
    _numbers.clear();
    _strings.clear();
    _ipaddrs.clear();
    _cidrs.clear();
    _builtinFunctions.clear();
}

void IRProgram::dump()
{
    std::cerr << dumpToString();
}

std::string IRProgram::dumpToString() const
{
    std::ostringstream sstr;
    sstr << "; IRProgram\n";

    for (auto const& function: _functions)
        sstr << function->dumpToString();

    return sstr.str();
}

IRFunction* IRProgram::createFunction(const std::string& name)
{
    _functions.emplace_back(std::make_unique<IRFunction>(name, this));
    return _functions.back().get();
}

void IRProgram::removeFunction(IRFunction* function)
{
    std::erase_if(_functions, [function](auto const& h) { return h.get() == function; });
}

// template ConstantInt* IRProgram::get<ConstantInt, int64_t>(std::vector<std::unique_ptr<ConstantInt>>&,
//                                                            int64_t&&);
//
// template ConstantArray* IRProgram::get<ConstantArray, std::vector<Constant*>>(std::vector<ConstantArray>&,
//                                                                               std::vector<Constant*>&&);
//
// template ConstantString* IRProgram::get<ConstantString, std::string>(
//     std::vector<std::unique_ptr<ConstantString>>&, std::string&&);
//
// template ConstantIP* IRProgram::get<ConstantIP, util::IPAddress>(std::vector<std::unique_ptr<ConstantIP>>&,
//                                                                  util::IPAddress&&);
//
// template ConstantCidr* IRProgram::get<ConstantCidr,
// util::Cidr>(std::vector<std::unique_ptr<ConstantCidr>>&,
//                                                                 util::Cidr&&);
//
// template ConstantRegExp* IRProgram::get<ConstantRegExp, util::RegExp>(
//     std::vector<std::unique_ptr<ConstantRegExp>>&, util::RegExp&&);

IRBuiltinFunction* IRProgram::getBuiltinFunction(const NativeCallback& cb)
{
    for (const auto& builtinFunction: _builtinFunctions)
        if (builtinFunction->signature() == cb.signature())
            return builtinFunction.get();

    _builtinFunctions.emplace_back(std::make_unique<IRBuiltinFunction>(cb));
    return _builtinFunctions.back().get();
}

} // namespace CoreVM
