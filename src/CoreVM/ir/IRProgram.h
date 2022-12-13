// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <CoreVM/Signature.h>
#include <CoreVM/ir/ConstantArray.h>
#include <CoreVM/ir/ConstantValue.h>
#include <CoreVM/ir/IRBuiltinFunction.h>
#include <CoreVM/ir/IRBuiltinHandler.h>
#include <CoreVM/ir/IRHandler.h>
#include <CoreVM/ir/Instructions.h>
#include <CoreVM/ir/Value.h>
#include <CoreVM/util/Cidr.h>
#include <CoreVM/util/IPAddress.h>
#include <CoreVM/util/RegExp.h>
#include <CoreVM/util/unbox.h>

#include <ranges>
#include <string>
#include <vector>

namespace CoreVM
{

class IRBuilder;
class ConstantArray;

class IRProgram
{
  public:
    IRProgram();
    ~IRProgram();

    void dump();

    ConstantBoolean* getBoolean(bool literal) { return literal ? &_trueLiteral : &_falseLiteral; }
    ConstantInt* get(int64_t literal) { return get<ConstantInt>(_numbers, literal); }
    ConstantString* get(const std::string& literal) { return get<ConstantString>(_strings, literal); }
    ConstantIP* get(const util::IPAddress& literal) { return get<ConstantIP>(_ipaddrs, literal); }
    ConstantCidr* get(const util::Cidr& literal) { return get<ConstantCidr>(_cidrs, literal); }
    ConstantRegExp* get(const util::RegExp& literal) { return get<ConstantRegExp>(_regexps, literal); }
    ConstantArray* get(const std::vector<Constant*>& elems)
    {
        return get<ConstantArray>(_constantArrays, elems);
    }

    [[nodiscard]] IRBuiltinHandler* findBuiltinHandler(const Signature& sig) const
    {
        for (const auto& builtinHandler: _builtinHandlers)
            if (builtinHandler->signature() == sig)
                return builtinHandler.get();

        return nullptr;
    }

    IRBuiltinHandler* getBuiltinHandler(const NativeCallback& cb)
    {
        for (const auto& builtinHandler: _builtinHandlers)
            if (builtinHandler->signature() == cb.signature())
                return builtinHandler.get();

        _builtinHandlers.emplace_back(std::make_unique<IRBuiltinHandler>(cb));
        return _builtinHandlers.back().get();
    }

    IRBuiltinFunction* getBuiltinFunction(const NativeCallback& cb)
    {
        for (const auto& builtinFunction: _builtinFunctions)
            if (builtinFunction->signature() == cb.signature())
                return builtinFunction.get();

        _builtinFunctions.emplace_back(std::make_unique<IRBuiltinFunction>(cb));
        return _builtinFunctions.back().get();
    }

    template <typename T, typename U>
    T* get(std::vector<T>& table, U&& literal);
    template <typename T, typename U>
    T* get(std::vector<std::unique_ptr<T>>& table, U&& literal);

    void addImport(const std::string& name, const std::string& path) { _modules.emplace_back(name, path); }
    void setModules(const std::vector<std::pair<std::string, std::string>>& modules) { _modules = modules; }

    [[nodiscard]] const std::vector<std::pair<std::string, std::string>>& modules() const { return _modules; }

    auto handlers() { return util::unbox(_handlers); }

    IRHandler* findHandler(const std::string& name)
    {
        for (IRHandler* handler: handlers())
            if (handler->name() == name)
                return handler;

        return nullptr;
    }

    IRHandler* createHandler(const std::string& name);

    /**
     * Performs given transformation on all handlers by given type.
     */
    template <typename TheHandlerPass, typename... Args>
    size_t transform(Args&&... args)
    {
        size_t count = 0;
        for (IRHandler* handler: handlers())
        {
            count += handler->transform<TheHandlerPass>(args...);
        }
        return count;
    }

  private:
    std::vector<std::pair<std::string, std::string>> _modules;
    ConstantBoolean _trueLiteral;
    ConstantBoolean _falseLiteral;
    std::vector<std::unique_ptr<ConstantArray>> _constantArrays;
    std::vector<std::unique_ptr<ConstantInt>> _numbers;
    std::vector<std::unique_ptr<ConstantString>> _strings;
    std::vector<std::unique_ptr<ConstantIP>> _ipaddrs;
    std::vector<std::unique_ptr<ConstantCidr>> _cidrs;
    std::vector<std::unique_ptr<ConstantRegExp>> _regexps;
    std::vector<std::unique_ptr<IRBuiltinFunction>> _builtinFunctions;
    std::vector<std::unique_ptr<IRBuiltinHandler>> _builtinHandlers;
    std::vector<std::unique_ptr<IRHandler>> _handlers;

    friend class IRBuilder;
};

template <typename T, typename U>
T* IRProgram::get(std::vector<std::unique_ptr<T>>& table, U&& literal)
{
    for (size_t i = 0, e = table.size(); i != e; ++i)
        if (table[i]->get() == literal)
            return table[i].get();

    table.emplace_back(std::make_unique<T>(std::forward<U>(literal)));
    return table.back().get();
}

template <typename T, typename U>
T* IRProgram::get(std::vector<T>& table, U&& literal)
{
    if (auto i = std::ranges::find_if(table, [&](T const& elem) { return elem.get() == literal; });
        i != table.end())
    {
        fmt::print("IRProgram.get<{}, {}>: found existing entry\n", typeid(T).name(), typeid(U).name());
        return &*i;
    }

    // for (size_t i = 0, e = table.size(); i != e; ++i)
    //     if (table[i].get() == literal)
    //     {
    //         fmt::print("IRProgram.get<{}, {}>: found existing entry\n", typeid(T).name(),
    //         typeid(U).name()); return &table[i];
    //     }

    fmt::print("IRProgram.get<{}, {}>: creating new entry\n", typeid(T).name(), typeid(U).name());
    table.emplace_back(T { std::forward<U>(literal) });
    for (auto const& elem: literal)
        fmt::print(" - {}\n", elem->to_string());
    return &table.back();
}

} // namespace CoreVM
