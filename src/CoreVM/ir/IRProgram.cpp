// SPDX-License-Identifier: Apache-2.0

#include <CoreVM/ir/ConstantArray.h>
#include <CoreVM/ir/IRHandler.h>
#include <CoreVM/ir/IRProgram.h>

#include <cassert>

namespace CoreVM
{

IRProgram::IRProgram(): _trueLiteral(true, "trueLiteral"), _falseLiteral(false, "falseLiteral")
{
}

#define GLOBAL_SCOPE_INIT_NAME "@__global_init__"

IRProgram::~IRProgram()
{
    // first reset all standard handlers and *then* the global-scope initialization handler
    // in order to not cause confusion upon resource release
    {
        std::unique_ptr<IRHandler> global;
        auto gh = std::find_if(_handlers.begin(), _handlers.end(), [](auto& handler) {
            return handler->name() == GLOBAL_SCOPE_INIT_NAME;
        });
        if (gh != _handlers.end())
        {
            global = std::move(*gh);
            _handlers.erase(gh);
        }
        _handlers.clear();
        global.reset(nullptr);
    }

    _constantArrays.clear();
    _numbers.clear();
    _strings.clear();
    _ipaddrs.clear();
    _cidrs.clear();
    _builtinHandlers.clear();
    _builtinFunctions.clear();
}

void IRProgram::dump()
{
    printf("; IRProgram\n");

    for (auto& handler: _handlers)
        handler->dump();
}

IRHandler* IRProgram::createHandler(const std::string& name)
{
    _handlers.emplace_back(std::make_unique<IRHandler>(name, this));
    return _handlers.back().get();
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
// template ConstantCidr* IRProgram::get<ConstantCidr, util::Cidr>(std::vector<std::unique_ptr<ConstantCidr>>&,
//                                                                 util::Cidr&&);
//
// template ConstantRegExp* IRProgram::get<ConstantRegExp, util::RegExp>(
//     std::vector<std::unique_ptr<ConstantRegExp>>&, util::RegExp&&);

} // namespace CoreVM
