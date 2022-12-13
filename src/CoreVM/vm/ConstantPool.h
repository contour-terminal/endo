// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <CoreVM/LiteralType.h>
#include <CoreVM/util/Cidr.h>
#include <CoreVM/util/IPAddress.h>
#include <CoreVM/util/RegExp.h>
#include <CoreVM/vm/Match.h>

namespace CoreVM
{

class IRHandler;
class IRBuiltinFunction;
class IRBuiltinHandler;

/**
 * Provides a pool of constant that can be built dynamically during code
 * generation and accessed effeciently at runtime.
 *
 * @see Program
 */
class ConstantPool
{
  public:
    using Code = std::vector<Instruction>;

    ConstantPool(const ConstantPool& v) = delete;
    ConstantPool& operator=(const ConstantPool& v) = delete;

    ConstantPool() = default;
    ConstantPool(ConstantPool&& from) noexcept = default;
    ConstantPool& operator=(ConstantPool&& v) noexcept = default;

    // builder
    size_t makeInteger(CoreNumber value);
    size_t makeString(const std::string& value);
    size_t makeIPAddress(const util::IPAddress& value);
    size_t makeCidr(const util::Cidr& value);
    size_t makeRegExp(const util::RegExp& value);

    size_t makeIntegerArray(const std::vector<CoreNumber>& elements);
    size_t makeStringArray(const std::vector<std::string>& elements);
    size_t makeIPaddrArray(const std::vector<util::IPAddress>& elements);
    size_t makeCidrArray(const std::vector<util::Cidr>& elements);

    size_t makeMatchDef();
    MatchDef& getMatchDef(size_t id) { return _matchDefs[id]; }

    size_t makeNativeHandler(const std::string& sig);
    size_t makeNativeHandler(const IRBuiltinHandler* handler);
    size_t makeNativeFunction(const std::string& sig);
    size_t makeNativeFunction(const IRBuiltinFunction* function);

    size_t makeHandler(const std::string& handlerName);
    size_t makeHandler(const IRHandler* handler);

    void setModules(const std::vector<std::pair<std::string, std::string>>& modules) { _modules = modules; }

    // accessor
    CoreNumber getInteger(size_t id) const { return _numbers[id]; }
    const CoreString& getString(size_t id) const { return _strings[id]; }
    const util::IPAddress& getIPAddress(size_t id) const { return _ipaddrs[id]; }
    const util::Cidr& getCidr(size_t id) const { return _cidrs[id]; }
    const util::RegExp& getRegExp(size_t id) const { return _regularExpressions[id]; }

    const std::vector<CoreNumber>& getIntArray(size_t id) const { return _intArrays[id]; }
    const std::vector<std::string>& getStringArray(size_t id) const { return _stringArrays[id]; }
    const std::vector<util::IPAddress>& getIPAddressArray(size_t id) const { return _ipaddrArrays[id]; }
    const std::vector<util::Cidr>& getCidrArray(size_t id) const { return _cidrArrays[id]; }

    const MatchDef& getMatchDef(size_t id) const { return _matchDefs[id]; }

    const std::pair<std::string, Code>& getHandler(size_t id) const { return _handlers[id]; }
    std::pair<std::string, Code>& getHandler(size_t id) { return _handlers[id]; }

    size_t setHandler(const std::string& name, Code&& code)
    {
        auto id = makeHandler(name);
        _handlers[id].second = std::move(code);
        return id;
    }

    // bulk accessors
    const std::vector<std::pair<std::string, std::string>>& getModules() const { return _modules; }
    const std::vector<std::pair<std::string, Code>>& getHandlers() const { return _handlers; }
    const std::vector<MatchDef>& getMatchDefs() const { return _matchDefs; }
    const std::vector<std::string>& getNativeHandlerSignatures() const { return _nativeHandlerSignatures; }
    const std::vector<std::string>& getNativeFunctionSignatures() const { return _nativeFunctionSignatures; }

    void dump() const;

  private:
    // constant primitives
    std::vector<CoreNumber> _numbers;
    std::vector<std::string> _strings;
    std::vector<util::IPAddress> _ipaddrs;
    std::vector<util::Cidr> _cidrs;
    std::vector<util::RegExp> _regularExpressions;

    // constant arrays
    std::vector<std::vector<CoreNumber>> _intArrays;
    std::vector<std::vector<std::string>> _stringArrays;
    std::vector<std::vector<util::IPAddress>> _ipaddrArrays;
    std::vector<std::vector<util::Cidr>> _cidrArrays;

    // code data
    std::vector<std::pair<std::string, std::string>> _modules;
    std::vector<std::pair<std::string, Code>> _handlers;
    std::vector<MatchDef> _matchDefs;
    std::vector<std::string> _nativeHandlerSignatures;
    std::vector<std::string> _nativeFunctionSignatures;
};

} // namespace CoreVM
