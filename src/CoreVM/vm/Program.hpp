// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <CoreVM/CoreTypes.hpp>
#include <CoreVM/SourceLocation.hpp>
#include <CoreVM/enums.hpp>
#include <CoreVM/types/TypeRegistry.hpp>
#include <CoreVM/util.hpp>

#include <cstdint>
#include <expected>
#include <format>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace CoreVM
{

class Function;
class NativeCallback;
class Runner;
class Runtime;
class IRBuiltinFunction;
class IRFunction;

/// Debug information for a single variable (alloca) within a function.
struct DebugVarInfo
{
    std::string name;
    size_t allocaIndex = 0;
    LiteralType type = LiteralType::Void;
};

namespace diagnostics
{
    class Report;
}

// =============================================================================
// RuntimeError
// =============================================================================

/// Runtime error with source location for user-friendly error reporting.
struct RuntimeError
{
    std::string message;
    SourceLocation location;

    /// Format the error for display to the user
    [[nodiscard]] std::string format() const
    {
        if (location.filename.empty())
            return std::format("runtime error: {}", message);
        return std::format("{}:{}:{}: runtime error: {}",
                           location.filename,
                           location.begin.line,
                           location.begin.column,
                           message);
    }
};

// =============================================================================
// Match types
// =============================================================================

struct MatchCaseDef
{
    uint64_t label {};
    uint64_t pc {};

    MatchCaseDef() = default;

    explicit MatchCaseDef(uint64_t l): label(l) {}

    MatchCaseDef(uint64_t l, uint64_t p): label(l), pc(p) {}
};

class MatchDef
{
  public:
    size_t functionId {};
    MatchClass op {};
    uint64_t elsePC {};
    std::vector<MatchCaseDef> cases;
};

class Match
{
  public:
    explicit Match(MatchDef def);
    virtual ~Match() = default;

    [[nodiscard]] const MatchDef& def() const { return _def; }

    virtual uint64_t evaluate(const CoreString* condition, Runner* env) const = 0;

  protected:
    MatchDef _def;
};

// =============================================================================
// ConstantPool
// =============================================================================

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
    size_t makeFloat(double value);
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

    size_t makeNativeFunction(const std::string& sig);
    size_t makeNativeFunction(const IRBuiltinFunction* function);

    size_t makeFunction(const std::string& functionName);
    size_t makeFunction(const IRFunction* function);

    void setModules(const std::vector<std::pair<std::string, std::string>>& modules) { _modules = modules; }

    // accessor
    [[nodiscard]] CoreNumber getInteger(size_t id) const { return _numbers[id]; }

    [[nodiscard]] double getFloat(size_t id) const { return _floats[id]; }

    [[nodiscard]] const CoreString& getString(size_t id) const { return _strings[id]; }

    [[nodiscard]] const util::IPAddress& getIPAddress(size_t id) const { return _ipaddrs[id]; }

    [[nodiscard]] const util::Cidr& getCidr(size_t id) const { return _cidrs[id]; }

    [[nodiscard]] const util::RegExp& getRegExp(size_t id) const { return _regularExpressions[id]; }

    [[nodiscard]] const std::vector<CoreNumber>& getIntArray(size_t id) const { return _intArrays[id]; }

    [[nodiscard]] const std::vector<std::string>& getStringArray(size_t id) const
    {
        return _stringArrays[id];
    }

    [[nodiscard]] const std::vector<util::IPAddress>& getIPAddressArray(size_t id) const
    {
        return _ipaddrArrays[id];
    }

    [[nodiscard]] const std::vector<util::Cidr>& getCidrArray(size_t id) const { return _cidrArrays[id]; }

    [[nodiscard]] const MatchDef& getMatchDef(size_t id) const { return _matchDefs[id]; }

    [[nodiscard]] const std::pair<std::string, Code>& getFunction(size_t id) const { return _functions[id]; }

    std::pair<std::string, Code>& getFunction(size_t id) { return _functions[id]; }

    size_t setFunction(const std::string& name, Code&& code)
    {
        auto id = makeFunction(name);
        _functions[id].second = std::move(code);
        return id;
    }

    // bulk accessors
    [[nodiscard]] const std::vector<std::pair<std::string, std::string>>& getModules() const
    {
        return _modules;
    }

    [[nodiscard]] const std::vector<std::pair<std::string, Code>>& getFunctions() const { return _functions; }

    /// Location table for each function.
    using LocationTable = std::vector<std::pair<size_t, SourceLocation>>;

    void setFunctionLocationTable(size_t functionId, LocationTable table)
    {
        if (_functionLocationTables.size() <= functionId)
            _functionLocationTables.resize(functionId + 1);
        _functionLocationTables[functionId] = std::move(table);
    }

    [[nodiscard]] LocationTable const& getFunctionLocationTable(size_t functionId) const
    {
        static LocationTable const empty;
        return functionId < _functionLocationTables.size() ? _functionLocationTables[functionId] : empty;
    }

    void setFunctionParameterCount(size_t functionId, size_t count)
    {
        if (_functionParamCounts.size() <= functionId)
            _functionParamCounts.resize(functionId + 1, 0);
        _functionParamCounts[functionId] = count;
    }

    [[nodiscard]] size_t getFunctionParameterCount(size_t functionId) const
    {
        return functionId < _functionParamCounts.size() ? _functionParamCounts[functionId] : 0;
    }

    /// Stores debug variable information for a compiled function.
    void setFunctionDebugVarInfo(size_t functionId, std::vector<DebugVarInfo> info)
    {
        if (_functionDebugVarInfo.size() <= functionId)
            _functionDebugVarInfo.resize(functionId + 1);
        _functionDebugVarInfo[functionId] = std::move(info);
    }

    /// Returns debug variable information for a compiled function.
    [[nodiscard]] std::vector<DebugVarInfo> const& getFunctionDebugVarInfo(size_t functionId) const
    {
        static std::vector<DebugVarInfo> const empty;
        return functionId < _functionDebugVarInfo.size() ? _functionDebugVarInfo[functionId] : empty;
    }

    [[nodiscard]] const std::vector<MatchDef>& getMatchDefs() const { return _matchDefs; }

    [[nodiscard]] const std::vector<std::string>& getNativeFunctionSignatures() const
    {
        return _nativeFunctionSignatures;
    }

    void dump() const;
    [[nodiscard]] std::string dumpToString() const;

    [[nodiscard]] TypeRegistry& typeRegistry() noexcept { return _typeRegistry; }

    [[nodiscard]] const TypeRegistry& typeRegistry() const noexcept { return _typeRegistry; }

  private:
    std::vector<CoreNumber> _numbers;
    std::vector<double> _floats;
    std::vector<std::string> _strings;
    std::vector<util::IPAddress> _ipaddrs;
    std::vector<util::Cidr> _cidrs;
    std::vector<util::RegExp> _regularExpressions;

    std::vector<std::vector<CoreNumber>> _intArrays;
    std::vector<std::vector<std::string>> _stringArrays;
    std::vector<std::vector<util::IPAddress>> _ipaddrArrays;
    std::vector<std::vector<util::Cidr>> _cidrArrays;

    std::vector<std::pair<std::string, std::string>> _modules;
    std::vector<std::pair<std::string, Code>> _functions;
    std::vector<LocationTable> _functionLocationTables;
    std::vector<size_t> _functionParamCounts;
    std::vector<MatchDef> _matchDefs;
    std::vector<std::string> _nativeFunctionSignatures;

    std::vector<std::vector<DebugVarInfo>> _functionDebugVarInfo;

    TypeRegistry _typeRegistry;
};

// =============================================================================
// Program
// =============================================================================

class Program
{
  public:
    explicit Program(ConstantPool&& cp);
    Program(Program&) = delete;
    Program& operator=(Program&) = delete;
    ~Program() = default;

    const ConstantPool& constants() const noexcept { return _cp; }

    ConstantPool& constants() noexcept { return _cp; }

    TypeRegistry& mutableTypeRegistry() noexcept { return _cp.typeRegistry(); }

    const Match* match(size_t index) const { return _matches[index].get(); }

    Function* function(size_t index) const;

    NativeCallback* nativeFunction(size_t index) const { return _nativeFunctions[index]; }

    auto matches() { return util::unbox(_matches); }

    std::vector<std::string> functionNames() const;
    int indexOf(const Function* that) const noexcept;
    Function* findFunction(const std::string& name) const noexcept;

    bool link(Runtime* runtime, diagnostics::Report* report);

    void dump();
    [[nodiscard]] std::string dumpToString() const;

    using Code = ConstantPool::Code;
    Function* createFunction(const std::string& name, const Code& code);

  private:
    void setup();
    Function* createFunction(const std::string& name);

  private:
    ConstantPool _cp;

    Runtime* _runtime = nullptr;
    mutable std::vector<std::unique_ptr<Function>> _functions;
    std::vector<std::unique_ptr<Match>> _matches;
    std::vector<NativeCallback*> _nativeFunctions;
};

// =============================================================================
// Function
// =============================================================================

class Function
{
  public:
    Function(Program* program, std::string name, std::vector<Instruction> code);
    Function() = default;
    Function(const Function&) = delete;
    Function(Function&&) noexcept = default;
    Function& operator=(const Function&) = delete;
    Function& operator=(Function&&) noexcept = default;
    ~Function() = default;

    [[nodiscard]] Program* program() const noexcept { return _program; }

    [[nodiscard]] const std::string& name() const noexcept { return _name; }

    void setName(const std::string& name) { _name = name; }

    [[nodiscard]] size_t stackSize() const noexcept { return _stackSize; }

    [[nodiscard]] const std::vector<Instruction>& code() const noexcept { return _code; }

    void setCode(std::vector<Instruction> code);

#if defined(COREVM_DIRECT_THREADED_VM)
    [[nodiscard]] const std::vector<uint64_t>& directThreadedCode() const noexcept
    {
        return _directThreadedCode;
    }

    [[nodiscard]] std::vector<uint64_t>& directThreadedCode() noexcept { return _directThreadedCode; }
#endif

    [[nodiscard]] size_t parameterCount() const noexcept { return _parameterCount; }

    void setParameterCount(size_t count) { _parameterCount = count; }

    void disassemble() const noexcept;

    void setLocationTable(std::vector<std::pair<size_t, SourceLocation>> table);
    [[nodiscard]] SourceLocation const& locationOf(size_t offset) const;

    /// Returns the raw location table for diagnostics and testing.
    [[nodiscard]] std::vector<std::pair<size_t, SourceLocation>> const* locationTable() const noexcept
    {
        return _locationTable.get();
    }

    /// Dumps the location table to stdout for debugging.
    void dumpLocationTable() const noexcept;

  private:
    Program* _program {};
    std::string _name;
    size_t _stackSize {};
    size_t _parameterCount {};
    std::vector<Instruction> _code;
    std::unique_ptr<std::vector<std::pair<size_t, SourceLocation>>> _locationTable;
#if defined(COREVM_DIRECT_THREADED_VM)
    std::vector<uint64_t> _directThreadedCode;
#endif
};

// =============================================================================
// Match subclasses
// =============================================================================

class MatchSame: public Match
{
  public:
    MatchSame(const MatchDef& def, Program* program);
    uint64_t evaluate(const CoreString* condition, Runner* env) const override;

  private:
    std::unordered_map<CoreString, uint64_t> _map;
};

class MatchHead: public Match
{
  public:
    MatchHead(const MatchDef& def, Program* program);
    uint64_t evaluate(const CoreString* condition, Runner* env) const override;

  private:
    util::PrefixTree<CoreString, uint64_t> _map;
};

class MatchTail: public Match
{
  public:
    MatchTail(const MatchDef& def, Program* program);
    uint64_t evaluate(const CoreString* condition, Runner* env) const override;

  private:
    util::SuffixTree<CoreString, uint64_t> _map;
};

class MatchRegEx: public Match
{
  public:
    MatchRegEx(const MatchDef& def, Program* program);
    uint64_t evaluate(const CoreString* condition, Runner* env) const override;

  private:
    std::vector<std::pair<util::RegExp, uint64_t>> _map;
};

} // namespace CoreVM
