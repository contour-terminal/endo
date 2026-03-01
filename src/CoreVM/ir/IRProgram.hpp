// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <CoreVM/SourceLocation.hpp>
#include <CoreVM/enums.hpp>
#include <CoreVM/ir/BasicBlock.hpp>
#include <CoreVM/ir/Instructions.hpp>
#include <CoreVM/ir/Value.hpp>
#include <CoreVM/types/TypeDescriptor.hpp>
#include <CoreVM/util.hpp>

#include <algorithm>
#include <format>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace CoreVM
{

class NativeCallback;

// =============================================================================
// PassManager
// =============================================================================

class PassManager
{
  public:
    using FunctionPass = std::function<bool(IRFunction* function)>;

    PassManager() = default;
    ~PassManager() = default;

    void registerPass(std::string name, FunctionPass functionPass);

    void run(IRProgram* program);
    void run(IRFunction* function);

    template <typename... Args>
    void logDebug(std::format_string<Args...> msg, Args... args)
    {
        logDebug(std::vformat(msg.get(), std::make_format_args(args...)));
    }

    void logDebug(const std::string& msg);

  private:
    std::list<std::pair<std::string, FunctionPass>> _functionPasses;
};

// =============================================================================
// IRFunction
// =============================================================================

class IRFunction: public Constant
{
  public:
    IRFunction(const std::string& name, IRProgram* parent);
    ~IRFunction() override;

    IRFunction(IRFunction&&) = delete;
    IRFunction& operator=(IRFunction&&) = delete;

    BasicBlock* createBlock(const std::string& name = "");

    [[nodiscard]] IRProgram* getProgram() const { return _program; }

    void setParent(IRProgram* prog) { _program = prog; }

    void dump();
    [[nodiscard]] std::string dumpToString() const;

    [[nodiscard]] bool empty() const noexcept { return _blocks.empty(); }

    [[nodiscard]] size_t parameterCount() const noexcept { return _parameterCount; }

    void setParameterCount(size_t count) { _parameterCount = count; }

    auto basicBlocks() { return util::unbox(_blocks); }

    [[nodiscard]] BasicBlock* getEntryBlock() const { return _blocks.front().get(); }

    void setEntryBlock(BasicBlock* bb);

    void erase(BasicBlock* bb);

    bool isAfter(const BasicBlock* bb, const BasicBlock* afterThat) const;
    void moveAfter(const BasicBlock* moveable, const BasicBlock* afterThat);
    void moveBefore(const BasicBlock* moveable, const BasicBlock* beforeThat);

    template <typename TheFunctionPass, typename... Args>
    size_t transform(Args&&... args)
    {
        return TheFunctionPass(std::forward(args)...).run(this);
    }

    void verify();

  private:
    IRProgram* _program;
    std::list<std::unique_ptr<BasicBlock>> _blocks;
    size_t _parameterCount = 0;

    friend class IRBuilder;
};

// =============================================================================
// IRProgram
// =============================================================================

class IRProgram
{
  public:
    IRProgram();
    ~IRProgram();

    void dump();
    [[nodiscard]] std::string dumpToString() const;

    ConstantBoolean* getBoolean(bool literal) { return literal ? &_trueLiteral : &_falseLiteral; }

    ConstantInt* get(int64_t literal) { return get<ConstantInt>(_numbers, literal); }

    ConstantFloat* getFloat(double literal) { return get<ConstantFloat>(_floats, literal); }

    ConstantString* get(const std::string& literal) { return get<ConstantString>(_strings, literal); }

    ConstantIP* get(const util::IPAddress& literal) { return get<ConstantIP>(_ipaddrs, literal); }

    ConstantCidr* get(const util::Cidr& literal) { return get<ConstantCidr>(_cidrs, literal); }

    ConstantRegExp* get(const util::RegExp& literal) { return get<ConstantRegExp>(_regexps, literal); }

    ConstantArray* get(const std::vector<Constant*>& elems)
    {
        return get<ConstantArray>(_constantArrays, elems);
    }

    IRBuiltinFunction* getBuiltinFunction(const NativeCallback& cb);

    template <typename T, typename U>
    T* get(std::vector<T>& table, U&& literal);
    template <typename T, typename U>
    T* get(std::vector<std::unique_ptr<T>>& table, U&& literal);

    void addImport(const std::string& name, const std::string& path) { _modules.emplace_back(name, path); }

    void setModules(const std::vector<std::pair<std::string, std::string>>& modules) { _modules = modules; }

    [[nodiscard]] const std::vector<std::pair<std::string, std::string>>& modules() const { return _modules; }

    auto functions() { return util::unbox(_functions); }

    IRFunction* findFunction(const std::string& name)
    {
        for (IRFunction* fn: functions())
            if (fn->name() == name)
                return fn;

        return nullptr;
    }

    IRFunction* createFunction(const std::string& name);

    void removeFunction(IRFunction* function);

    template <typename TheFunctionPass, typename... Args>
    size_t transform(Args&&... args)
    {
        size_t count = 0;
        for (IRFunction* fn: functions())
        {
            count += fn->transform<TheFunctionPass>(args...);
        }
        return count;
    }

    /// Describes a custom product type to be registered at target code generation time.
    struct CustomProductType
    {
        std::string name;
        std::vector<FieldInfo> fields;
        uint16_t assignedId;
        uint16_t slotCount = 0;
    };

    void addCustomProductType(CustomProductType def) { _customProductTypes.push_back(std::move(def)); }

    [[nodiscard]] std::vector<CustomProductType> const& customProductTypes() const
    {
        return _customProductTypes;
    }

    /// Describes a custom sum type (discriminated union) to be registered at target code generation time.
    struct CustomSumType
    {
        std::string name;
        std::vector<VariantInfo> variants;
        uint16_t assignedId;
    };

    void addCustomSumType(CustomSumType def) { _customSumTypes.push_back(std::move(def)); }

    [[nodiscard]] std::vector<CustomSumType> const& customSumTypes() const { return _customSumTypes; }

    [[nodiscard]] uint16_t allocateCustomTypeId() { return _nextCustomTypeId++; }

  private:
    std::vector<std::pair<std::string, std::string>> _modules;
    ConstantBoolean _trueLiteral;
    ConstantBoolean _falseLiteral;
    std::vector<std::unique_ptr<ConstantArray>> _constantArrays;
    std::vector<std::unique_ptr<ConstantInt>> _numbers;
    std::vector<std::unique_ptr<ConstantFloat>> _floats;
    std::vector<std::unique_ptr<ConstantString>> _strings;
    std::vector<std::unique_ptr<ConstantIP>> _ipaddrs;
    std::vector<std::unique_ptr<ConstantCidr>> _cidrs;
    std::vector<std::unique_ptr<ConstantRegExp>> _regexps;
    std::vector<std::unique_ptr<IRBuiltinFunction>> _builtinFunctions;
    std::vector<std::unique_ptr<IRFunction>> _functions;
    std::vector<CustomProductType> _customProductTypes;
    std::vector<CustomSumType> _customSumTypes;
    uint16_t _nextCustomTypeId = BuiltinTypeId::LastBuiltin + 1;

    friend class IRBuilder;
};

// =============================================================================
// IRBuilder
// =============================================================================

class IRBuilder
{
  private:
    std::unique_ptr<IRProgram> _program;
    IRFunction* _function;
    BasicBlock* _insertPoint;
    std::unordered_map<std::string, unsigned long> _nameStore;
    SourceLocation _currentLocation;

  public:
    IRBuilder();
    ~IRBuilder() = default;

    void setSourceLocation(SourceLocation loc) { _currentLocation = std::move(loc); }

    [[nodiscard]] SourceLocation const& sourceLocation() const noexcept { return _currentLocation; }

    std::string makeName(std::string name);

    void setProgram(std::unique_ptr<IRProgram> program);

    IRProgram* program() const { return _program.get(); }

    std::unique_ptr<IRProgram> takeProgram() { return std::move(_program); }

    IRFunction* setFunction(IRFunction* hn);

    IRFunction* function() const { return _function; }

    BasicBlock* createBlock(const std::string& name);

    void setInsertPoint(BasicBlock* bb);

    BasicBlock* getInsertPoint() const { return _insertPoint; }

    Instr* insert(std::unique_ptr<Instr> instr);

    template <typename T, typename... Args>
    T* insert(Args&&... args)
    {
        return static_cast<T*>(insert(std::make_unique<T>(std::forward<Args>(args)...)));
    }

    IRFunction* getFunction(const std::string& name);
    IRFunction* findFunction(const std::string& name);

    // literals
    ConstantBoolean* getBoolean(bool literal) { return _program->getBoolean(literal); }

    ConstantInt* get(int64_t literal) { return _program->get(literal); }

    ConstantFloat* getFloat(double literal) { return _program->getFloat(literal); }

    ConstantString* get(const std::string& literal) { return _program->get(literal); }

    ConstantIP* get(const util::IPAddress& literal) { return _program->get(literal); }

    ConstantCidr* get(const util::Cidr& literal) { return _program->get(literal); }

    ConstantRegExp* get(const util::RegExp& literal) { return _program->get(literal); }

    IRBuiltinFunction* getBuiltinFunction(const NativeCallback& cb)
    {
        return _program->getBuiltinFunction(cb);
    }

    ConstantArray* get(const std::vector<Constant*>& arrayElements) { return _program->get(arrayElements); }

    // values
    AllocaInstr* createAlloca(LiteralType ty, Value* arraySize, const std::string& name = "");
    Value* createLoad(Value* value, const std::string& name = "");
    Instr* createStore(Value* lhs, Value* rhs, const std::string& name = "");
    Instr* createStore(Value* lhs, ConstantInt* index, Value* rhs, const std::string& name = "");
    Instr* createPhi(const std::vector<Value*>& incomings, const std::string& name = "");

    // boolean operations
    Value* createBNot(Value* rhs, const std::string& name = "");
    Value* createBAnd(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createBOr(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createBXor(Value* lhs, Value* rhs, const std::string& name = "");

    // numerical operations
    Value* createNeg(Value* rhs, const std::string& name = "");
    Value* createNot(Value* rhs, const std::string& name = "");
    Value* createAdd(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createSub(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createMul(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createDiv(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createRem(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createShl(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createShr(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createPow(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createAnd(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createOr(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createXor(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createNCmpEQ(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createNCmpNE(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createNCmpLE(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createNCmpGE(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createNCmpLT(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createNCmpGT(Value* lhs, Value* rhs, const std::string& name = "");

    // float operations
    Value* createFNeg(Value* rhs, const std::string& name = "");
    Value* createFAdd(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createFSub(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createFMul(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createFDiv(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createFRem(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createFPow(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createFCmpEQ(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createFCmpNE(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createFCmpLE(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createFCmpGE(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createFCmpLT(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createFCmpGT(Value* lhs, Value* rhs, const std::string& name = "");

    // string ops
    Value* createSAdd(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createSCmpEQ(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createSCmpNE(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createSCmpLE(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createSCmpGE(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createSCmpLT(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createSCmpGT(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createSCmpRE(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createSCmpEB(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createSCmpEE(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createSIn(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createSLen(Value* value, const std::string& name = "");

    // IP address
    Value* createPCmpEQ(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createPCmpNE(Value* lhs, Value* rhs, const std::string& name = "");
    Value* createPInCidr(Value* lhs, Value* rhs, const std::string& name = "");

    // regexp
    RegExpGroupInstr* createRegExpGroup(ConstantInt* groupId, const std::string& name = "");

    // cast
    Value* createConvert(LiteralType ty, Value* rhs, const std::string& name = "");
    Value* createB2S(Value* rhs, const std::string& name = "");
    Value* createN2S(Value* rhs, const std::string& name = "");
    Value* createP2S(Value* rhs, const std::string& name = "");
    Value* createC2S(Value* rhs, const std::string& name = "");
    Value* createR2S(Value* rhs, const std::string& name = "");
    Value* createS2N(Value* rhs, const std::string& name = "");
    Value* createN2F(Value* rhs, const std::string& name = "");
    Value* createF2N(Value* rhs, const std::string& name = "");
    Value* createF2S(Value* rhs, const std::string& name = "");
    Value* createS2F(Value* rhs, const std::string& name = "");

    // calls
    Instr* createCallFunction(IRBuiltinFunction* callee, std::vector<Value*> args, std::string name = "");
    FunctionCallInstr* createFunctionCall(IRFunction* callee,
                                          std::vector<Value*> args,
                                          const std::string& name = "",
                                          LiteralType returnType = LiteralType::Void);
    FunctionRetInstr* createFunctionRet(Value* result, const std::string& name = "");
    TailCallInstr* createTailCall(IRFunction* callee, std::vector<Value*> args, const std::string& name = "");

    // termination instructions
    Instr* createRet(Value* result);
    Instr* createBr(BasicBlock* target);
    Instr* createCondBr(Value* condValue, BasicBlock* trueBlock, BasicBlock* falseBlock);
    MatchInstr* createMatch(MatchClass opc, Value* cond);
    Value* createMatchSame(Value* cond);
    Value* createMatchHead(Value* cond);
    Value* createMatchTail(Value* cond);
    Value* createMatchRegExp(Value* cond);

    // object operations
    ObjAllocInstr* createObjAlloc(ConstantInt* typeId, const std::string& name = "");
    ObjRetainInstr* createObjRetain(Value* object, const std::string& name = "");
    ObjReleaseInstr* createObjRelease(AllocaInstr* storage, const std::string& name = "");
    ObjGetTagInstr* createObjGetTag(Value* object, const std::string& name = "");
    ObjSetTagInstr* createObjSetTag(Value* object, Value* tag, const std::string& name = "");
    ObjGetSlotInstr* createObjGetSlot(Value* object, ConstantInt* slotIndex, const std::string& name = "");
    ObjSetSlotInstr* createObjSetSlot(Value* object,
                                      ConstantInt* slotIndex,
                                      Value* value,
                                      const std::string& name = "");
    ObjTypeIdInstr* createObjTypeId(Value* object, const std::string& name = "");
    ObjIsTypeInstr* createObjIsType(Value* object, ConstantInt* typeId, const std::string& name = "");

    // Dynamic value comparison
    VCmpEQInstr* createVCmpEQ(Value* lhs, Value* rhs, const std::string& name = "");
    VCmpNEInstr* createVCmpNE(Value* lhs, Value* rhs, const std::string& name = "");
    VCmpLTInstr* createVCmpLT(Value* lhs, Value* rhs, const std::string& name = "");
    VCmpLEInstr* createVCmpLE(Value* lhs, Value* rhs, const std::string& name = "");
    VCmpGTInstr* createVCmpGT(Value* lhs, Value* rhs, const std::string& name = "");
    VCmpGEInstr* createVCmpGE(Value* lhs, Value* rhs, const std::string& name = "");

    // Indirect function calls
    IndirectCallInstr* createIndirectCall(Value* callable,
                                          std::vector<Value*> args,
                                          const std::string& name = "");
    IndirectTailCallInstr* createIndirectTailCall(Value* callable,
                                                  std::vector<Value*> args,
                                                  const std::string& name = "");

    // Lazy evaluation
    FunctionRefInstr* createFunctionRef(IRFunction* function, const std::string& name = "");
    LazyForceInstr* createLazyForce(Value* lazyObj, const std::string& name = "");
};

// =============================================================================
// IRProgram template implementations
// =============================================================================

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
    if (auto i =
            std::find_if(table.begin(), table.end(), [&](T const& elem) { return elem.get() == literal; });
        i != table.end())
    {
        return &*i;
    }

    table.emplace_back(T { std::forward<U>(literal) });
    return &table.back();
}

} // namespace CoreVM
