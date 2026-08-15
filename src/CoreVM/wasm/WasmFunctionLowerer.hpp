// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <CoreVM/Diagnostics.hpp>
#include <CoreVM/ir/Instructions.hpp>
#include <CoreVM/wasm/WasmCodeGenerator.hpp>
#include <CoreVM/wasm/WasmRuntimeABI.hpp>
#include <CoreVM/wasm/WasmStringTable.hpp>

#include <cstdint>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <binaryen-c.h>

namespace CoreVM
{
class BasicBlock;
class IRFunction;
} // namespace CoreVM

namespace CoreVM::wasm
{

/// Lowers a single IRFunction into a WASM function of the module under
/// construction.
///
/// Lowering strategy:
/// - `transform::materializeCrossBlockValues` runs first, so SSA values never
///   cross basic-block boundaries except through allocas. Allocas map to WASM
///   locals, Load/Store to local.get/local.set.
/// - Every VM-visible value is canonically i64 (see WasmRuntimeABI.hpp).
///   Expressions are kept at their natural type (i32 comparisons, f64 float
///   arithmetic) and coerced lazily via asI64/asF64/asCond.
/// - Within a block, pure single-use values inline as binaryen expression
///   trees; side-effecting or multiply-used values are pinned to scratch
///   locals in IR order.
/// - Control flow is structured with binaryen's relooper: one relooper block
///   per BasicBlock, branches recorded while visiting terminators.
class WasmFunctionLowerer final: public InstructionVisitor
{
  public:
    /// @param module      the binaryen module under construction
    /// @param options     compilation options
    /// @param report      receives diagnostics (unsupported constructs, internal errors)
    /// @param usedHelpers accumulates the runtime helpers referenced by generated code
    WasmFunctionLowerer(BinaryenModuleRef module,
                        WasmOptions const& options,
                        diagnostics::Report& report,
                        std::set<RuntimeHelperDef const*>& usedHelpers,
                        WasmStringTable& strings,
                        std::unordered_map<int64_t, int64_t> const& slotCounts);

    /// Lowers @p function and adds it to the module.
    void lower(IRFunction* function);

    /// Mangles an IR function name (e.g. "@main") into its WASM function name
    /// (e.g. "fn$@main"), avoiding collisions with runtime helpers and _start.
    [[nodiscard]] static std::string mangledName(std::string_view irName);

    // misc
    void visit(NopInstr& instr) override;
    void visit(AllocaInstr& instr) override;
    void visit(StoreInstr& instr) override;
    void visit(LoadInstr& instr) override;
    void visit(PhiNode& instr) override;

    // calls
    void visit(CallInstr& instr) override;
    void visit(FunctionCallInstr& instr) override;
    void visit(FunctionRetInstr& instr) override;
    void visit(TailCallInstr& instr) override;

    // terminators
    void visit(CondBrInstr& instr) override;
    void visit(BrInstr& instr) override;
    void visit(RetInstr& instr) override;
    void visit(MatchInstr& instr) override;

    // regexp
    void visit(RegExpGroupInstr& instr) override;

    // type cast
    void visit(CastInstr& instr) override;

    // numeric
    void visit(INegInstr& instr) override { lowerUnaryOp(instr, instr.op()); }

    void visit(INotInstr& instr) override { lowerUnaryOp(instr, instr.op()); }

    void visit(IAddInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    void visit(ISubInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    void visit(IMulInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    void visit(IDivInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    void visit(IRemInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    void visit(IPowInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    void visit(IAndInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    void visit(IOrInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    void visit(IXorInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    void visit(IShlInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    void visit(IShrInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    void visit(ICmpEQInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    void visit(ICmpNEInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    void visit(ICmpLEInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    void visit(ICmpGEInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    void visit(ICmpLTInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    void visit(ICmpGTInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    // boolean
    void visit(BNotInstr& instr) override { lowerUnaryOp(instr, instr.op()); }

    void visit(BAndInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    void visit(BOrInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    void visit(BXorInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    // string
    void visit(SLenInstr& instr) override { lowerUnaryOp(instr, instr.op()); }

    void visit(SIsEmptyInstr& instr) override { lowerUnaryOp(instr, instr.op()); }

    void visit(SAddInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    void visit(SSubStrInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    void visit(SCmpEQInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    void visit(SCmpNEInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    void visit(SCmpLEInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    void visit(SCmpGEInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    void visit(SCmpLTInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    void visit(SCmpGTInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    void visit(SCmpREInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    void visit(SCmpBegInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    void visit(SCmpEndInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    void visit(SInInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    // ip
    void visit(PCmpEQInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    void visit(PCmpNEInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    void visit(PInCidrInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    // float
    void visit(FNegInstr& instr) override { lowerUnaryOp(instr, instr.op()); }

    void visit(FAddInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    void visit(FSubInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    void visit(FMulInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    void visit(FDivInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    void visit(FRemInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    void visit(FPowInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    void visit(FCmpEQInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    void visit(FCmpNEInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    void visit(FCmpLEInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    void visit(FCmpGEInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    void visit(FCmpLTInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    void visit(FCmpGTInstr& instr) override { lowerBinaryOp(instr, instr.op()); }

    // object operations
    void visit(ObjAllocInstr& instr) override;
    void visit(ObjRetainInstr& instr) override;
    void visit(ObjReleaseInstr& instr) override;
    void visit(ObjGetTagInstr& instr) override;
    void visit(ObjSetTagInstr& instr) override;
    void visit(ObjGetSlotInstr& instr) override;
    void visit(ObjSetSlotInstr& instr) override;
    void visit(ObjTypeIdInstr& instr) override;
    void visit(ObjIsTypeInstr& instr) override;

    // dynamic value comparison
    void visit(VCmpEQInstr& instr) override;
    void visit(VCmpNEInstr& instr) override;
    void visit(VCmpLTInstr& instr) override;
    void visit(VCmpLEInstr& instr) override;
    void visit(VCmpGTInstr& instr) override;
    void visit(VCmpGEInstr& instr) override;

    // indirect function calls
    void visit(IndirectCallInstr& instr) override;
    void visit(IndirectTailCallInstr& instr) override;

    // lazy evaluation
    void visit(FunctionRefInstr& instr) override;
    void visit(LazyForceInstr& instr) override;

  private:
    /// How an instruction's result expression is integrated into the output.
    enum class ResultMode : uint8_t
    {
        Pure,      ///< No side effects: inline if single-use, pin if multi-use, drop if unused.
        PinIfUsed, ///< No side effects but order-sensitive (e.g. loads): pin if used, drop if unused.
        Ordered,   ///< Side effects: evaluate at this program point (pin or Drop).
    };

    /// A conditional or unconditional edge between two basic blocks, recorded
    /// while visiting terminators and applied to the relooper afterwards.
    struct PendingBranch
    {
        BasicBlock* from = nullptr;
        BasicBlock* to = nullptr;
        Value* condition = nullptr;               ///< nullptr for the unconditional/default edge.
        Constant* caseLabel = nullptr;            ///< MatchInstr case: compare condition against this.
        MatchClass matchClass = MatchClass::Same; ///< MatchInstr case: comparison kind.
    };

    /// A value pinned into a scratch local.
    struct PinnedValue
    {
        uint32_t localIndex;
        BinaryenType type;
    };

    void assignLocals();

    /// Materializes an operand as a fresh binaryen expression.
    [[nodiscard]] BinaryenExpressionRef emitValue(Value* value);

    /// Integrates an instruction result according to @p mode.
    void setResult(Instr& instr, BinaryenExpressionRef expr, ResultMode mode);

    /// Forces @p value into a scratch local so it can be re-read later
    /// (used for terminator operands consumed during branch emission).
    void pinValue(Value* value);

    /// Moves @p expr into a fresh scratch local, recording it for @p value.
    void pin(Value& value, BinaryenExpressionRef expr);

    /// Allocates a new scratch local of the given type.
    [[nodiscard]] uint32_t scratchLocal(BinaryenType type);

    void pushStatement(BinaryenExpressionRef expr) { _statements.push_back(expr); }

    /// Emits a call to a runtime helper (recording it as used).
    [[nodiscard]] BinaryenExpressionRef callRuntime(std::string_view helperName,
                                                    std::span<BinaryenExpressionRef> args);

    /// A fresh i64 zero constant.
    [[nodiscard]] BinaryenExpressionRef zeroI64();

    /// Pins an object operand into a fresh i64 scratch local and returns its
    /// index, so the pointer can be used for a store and aliased as a result.
    [[nodiscard]] uint32_t pinObjectOperand(Value* object);

    /// The wrapped i32 pointer view of a pinned object local.
    [[nodiscard]] BinaryenExpressionRef pinnedPointer(uint32_t localIndex);

    /// Loads a header field or slot of an object operand (inlined address math
    /// over the unified cell header; see WasmRuntimeABI.hpp).
    [[nodiscard]] BinaryenExpressionRef loadObjectField(Value* object,
                                                        uint32_t bytes,
                                                        uint32_t offset,
                                                        BinaryenType type);

    /// Builds the i32 condition for one MatchInstr case edge.
    [[nodiscard]] BinaryenExpressionRef matchCaseCondition(Value* scrutinee,
                                                           Constant& caseLabel,
                                                           MatchClass matchClass);

    /// Lowers a raw-value comparison (VCmp*): compares the canonical i64 forms.
    void lowerValueCompare(Instr& instr, BinaryenOp compareOp);

    /// Coerces an expression to the canonical i64 representation.
    [[nodiscard]] BinaryenExpressionRef asI64(BinaryenExpressionRef expr);
    /// Coerces an expression to f64 (bit-cast from the canonical i64 form).
    [[nodiscard]] BinaryenExpressionRef asF64(BinaryenExpressionRef expr);
    /// Coerces an expression to an i32 branch condition (non-zero test).
    [[nodiscard]] BinaryenExpressionRef asCond(BinaryenExpressionRef expr);

    void lowerUnaryOp(Instr& instr, UnaryOperator op);
    void lowerBinaryOp(Instr& instr, BinaryOperator op);

    /// Reports an unsupported-construct diagnostic and poisons the result.
    void unsupported(Instr& instr, std::string_view what);
    /// Reports a backend bug (invariant violation), distinct from user-facing limitations.
    void internalError(std::string_view what);

    BinaryenModuleRef _module;
    WasmOptions const& _options;
    diagnostics::Report& _report;
    std::set<RuntimeHelperDef const*>& _usedHelpers;
    WasmStringTable& _strings;
    std::unordered_map<int64_t, int64_t> const& _slotCounts; ///< Object typeId -> slot count.

    IRFunction* _function = nullptr;
    BasicBlock* _currentBlock = nullptr;
    Instr* _currentInstr = nullptr;

    uint32_t _paramCount = 0;
    std::vector<BinaryenType> _varTypes; ///< Types of non-parameter locals (allocas + scratch).
    std::unordered_map<AllocaInstr const*, uint32_t> _localIndex;
    std::unordered_map<Value const*, BinaryenExpressionRef> _valueMap; ///< Un-consumed pure expressions.
    std::unordered_map<Value const*, PinnedValue> _pinned;
    std::vector<BinaryenExpressionRef> _statements;
    std::vector<PendingBranch> _pendingBranches;
    std::unordered_map<BasicBlock*, RelooperBlockRef> _relooperBlocks;
};

} // namespace CoreVM::wasm
