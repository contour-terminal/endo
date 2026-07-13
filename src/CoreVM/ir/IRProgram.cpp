// SPDX-License-Identifier: Apache-2.0
#include <CoreVM/CoreVM.hpp>
#include <CoreVM/types/TypeRegistry.hpp>

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
    // Detach every instruction from its operands before destroying any of them. A Value's
    // destructor asserts it has no remaining uses, but instructions are stored front-to-back
    // per block, so an alloca (at the front) is otherwise destroyed while the loads that
    // reference it (later, and possibly in other functions — e.g. capture-forwarding loads of
    // a global binding) are still alive. Clearing all use/def links up front makes teardown
    // order-independent.
    for (auto const& function: _functions)
        for (BasicBlock* bb: function->basicBlocks())
            for (Instr* instr: bb->instructions())
                instr->clearOperands();

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

void IRProgram::dump() const
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

void registerCustomTypes(IRProgram& program, TypeRegistry& registry)
{
    for (auto const& customType: program.customProductTypes())
    {
        auto type = std::make_unique<TypeDescriptor>();
        type->kind = TypeKind::Product;
        type->id = customType.assignedId;
        type->name = customType.name;
        type->fields = customType.fields;
        type->slotCount =
            customType.slotCount > 0 ? customType.slotCount : static_cast<uint16_t>(customType.fields.size());
        registry.registerProductType(std::move(type));
    }

    for (auto const& customType: program.customSumTypes())
    {
        auto type = std::make_unique<TypeDescriptor>();
        type->kind = TypeKind::Sum;
        type->id = customType.assignedId;
        type->name = customType.name;
        type->variants = customType.variants;
        registry.registerSumType(std::move(type));
    }
}

} // namespace CoreVM
