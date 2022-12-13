// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <CoreVM/MatchClass.h>
#include <CoreVM/Signature.h>
#include <CoreVM/ir/ConstantValue.h>
#include <CoreVM/ir/IRBuiltinFunction.h>
#include <CoreVM/ir/IRBuiltinHandler.h>
#include <CoreVM/ir/IRProgram.h>
#include <CoreVM/ir/Value.h>
#include <CoreVM/util/Cidr.h>
#include <CoreVM/util/IPAddress.h>
#include <CoreVM/util/RegExp.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace CoreVM
{

class Value;
class Instr;
class AllocaInstr;
class MatchInstr;
class BasicBlock;
class IRHandler;
class IRProgram;
class IRBuilder;
class ConstantArray;

class IRBuilder
{
  private:
    IRProgram* _program;
    IRHandler* _handler;
    BasicBlock* _insertPoint;
    std::unordered_map<std::string, unsigned long> _nameStore;

  public:
    IRBuilder();
    ~IRBuilder() = default;

    std::string makeName(std::string name);

    void setProgram(std::unique_ptr<IRProgram> program);
    IRProgram* program() const { return _program; }

    IRHandler* setHandler(IRHandler* hn);
    IRHandler* handler() const { return _handler; }

    BasicBlock* createBlock(const std::string& name);

    void setInsertPoint(BasicBlock* bb);
    BasicBlock* getInsertPoint() const { return _insertPoint; }

    Instr* insert(std::unique_ptr<Instr> instr);

    template <typename T, typename... Args>
    T* insert(Args&&... args)
    {
        return static_cast<T*>(insert(std::make_unique<T>(std::forward<Args>(args)...)));
    }

    IRHandler* getHandler(const std::string& name);
    IRHandler* findHandler(const std::string& name);

    // literals
    ConstantBoolean* getBoolean(bool literal) { return _program->getBoolean(literal); }
    ConstantInt* get(int64_t literal) { return _program->get(literal); }
    ConstantString* get(const std::string& literal) { return _program->get(literal); }
    ConstantIP* get(const util::IPAddress& literal) { return _program->get(literal); }
    ConstantCidr* get(const util::Cidr& literal) { return _program->get(literal); }
    ConstantRegExp* get(const util::RegExp& literal) { return _program->get(literal); }
    IRBuiltinHandler* findBuiltinHandler(const Signature& sig) { return _program->findBuiltinHandler(sig); }
    IRBuiltinHandler* getBuiltinHandler(const NativeCallback& cb) { return _program->getBuiltinHandler(cb); }
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
    Value* createBNot(Value* rhs, const std::string& name = ""); // !
    Value* createBAnd(Value* lhs, Value* rhs,
                      const std::string& name = ""); // &&
    Value* createBXor(Value* lhs, Value* rhs,
                      const std::string& name = ""); // ||

    // numerical operations
    Value* createNeg(Value* rhs, const std::string& name = "");             // -
    Value* createNot(Value* rhs, const std::string& name = "");             // ~
    Value* createAdd(Value* lhs, Value* rhs, const std::string& name = ""); // +
    Value* createSub(Value* lhs, Value* rhs, const std::string& name = ""); // -
    Value* createMul(Value* lhs, Value* rhs, const std::string& name = ""); // *
    Value* createDiv(Value* lhs, Value* rhs, const std::string& name = ""); // /
    Value* createRem(Value* lhs, Value* rhs, const std::string& name = ""); // %
    Value* createShl(Value* lhs, Value* rhs, const std::string& name = ""); // <<
    Value* createShr(Value* lhs, Value* rhs, const std::string& name = ""); // >>
    Value* createPow(Value* lhs, Value* rhs, const std::string& name = ""); // **
    Value* createAnd(Value* lhs, Value* rhs, const std::string& name = ""); // &
    Value* createOr(Value* lhs, Value* rhs, const std::string& name = "");  // |
    Value* createXor(Value* lhs, Value* rhs, const std::string& name = ""); // ^
    Value* createNCmpEQ(Value* lhs, Value* rhs,
                        const std::string& name = ""); // ==
    Value* createNCmpNE(Value* lhs, Value* rhs,
                        const std::string& name = ""); // !=
    Value* createNCmpLE(Value* lhs, Value* rhs,
                        const std::string& name = ""); // <=
    Value* createNCmpGE(Value* lhs, Value* rhs,
                        const std::string& name = ""); // >=
    Value* createNCmpLT(Value* lhs, Value* rhs,
                        const std::string& name = ""); // <
    Value* createNCmpGT(Value* lhs, Value* rhs,
                        const std::string& name = ""); // >

    // string ops
    Value* createSAdd(Value* lhs, Value* rhs, const std::string& name = ""); // +
    Value* createSCmpEQ(Value* lhs, Value* rhs,
                        const std::string& name = ""); // ==
    Value* createSCmpNE(Value* lhs, Value* rhs,
                        const std::string& name = ""); // !=
    Value* createSCmpLE(Value* lhs, Value* rhs,
                        const std::string& name = ""); // <=
    Value* createSCmpGE(Value* lhs, Value* rhs,
                        const std::string& name = ""); // >=
    Value* createSCmpLT(Value* lhs, Value* rhs,
                        const std::string& name = ""); // <
    Value* createSCmpGT(Value* lhs, Value* rhs,
                        const std::string& name = ""); // >
    Value* createSCmpRE(Value* lhs, Value* rhs,
                        const std::string& name = ""); // =~
    Value* createSCmpEB(Value* lhs, Value* rhs,
                        const std::string& name = ""); // =^
    Value* createSCmpEE(Value* lhs, Value* rhs,
                        const std::string& name = "");                      // =$
    Value* createSIn(Value* lhs, Value* rhs, const std::string& name = ""); // in
    Value* createSLen(Value* value, const std::string& name = "");

    // IP address
    Value* createPCmpEQ(Value* lhs, Value* rhs,
                        const std::string& name = ""); // ==
    Value* createPCmpNE(Value* lhs, Value* rhs,
                        const std::string& name = ""); // !=
    Value* createPInCidr(Value* lhs, Value* rhs,
                         const std::string& name = ""); // in

    // cidr
    // ...

    // regexp
    RegExpGroupInstr* createRegExpGroup(ConstantInt* groupId, const std::string& name = "");

    // cast
    Value* createConvert(LiteralType ty, Value* rhs,
                         const std::string& name = ""); // cast<T>()
    Value* createB2S(Value* rhs, const std::string& name = "");
    Value* createN2S(Value* rhs, const std::string& name = "");
    Value* createP2S(Value* rhs, const std::string& name = "");
    Value* createC2S(Value* rhs, const std::string& name = "");
    Value* createR2S(Value* rhs, const std::string& name = "");
    Value* createS2N(Value* rhs, const std::string& name = "");

    // calls
    Instr* createCallFunction(IRBuiltinFunction* callee,
                              std::vector<Value*> args,
                              std::string name = "");
    Instr* createInvokeHandler(IRBuiltinHandler* callee, const std::vector<Value*>& args);

    // termination instructions
    Instr* createRet(Value* result);
    Instr* createBr(BasicBlock* target);
    Instr* createCondBr(Value* condValue, BasicBlock* trueBlock, BasicBlock* falseBlock);
    MatchInstr* createMatch(MatchClass opc, Value* cond);
    Value* createMatchSame(Value* cond);
    Value* createMatchHead(Value* cond);
    Value* createMatchTail(Value* cond);
    Value* createMatchRegExp(Value* cond);
};

} // namespace CoreVM
