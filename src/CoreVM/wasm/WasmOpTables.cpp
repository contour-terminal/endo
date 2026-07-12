// SPDX-License-Identifier: Apache-2.0
#include <CoreVM/wasm/WasmOpTables.hpp>

#include <unordered_map>

namespace CoreVM::wasm
{

namespace
{
    using BKind = BinaryOpLowering::Kind;
    using UKind = UnaryOpLowering::Kind;

    // BinaryenOp values are only obtainable through function calls, so the
    // tables are function-local statics instead of constexpr arrays.
    std::unordered_map<BinaryOperator, BinaryOpLowering> makeBinaryOpTable()
    {
        return {
            // numerical
            { BinaryOperator::IAdd, { .kind = BKind::DirectOp, .op = &BinaryenAddInt64 } },
            { BinaryOperator::ISub, { .kind = BKind::DirectOp, .op = &BinaryenSubInt64 } },
            { BinaryOperator::IMul, { .kind = BKind::DirectOp, .op = &BinaryenMulInt64 } },
            { BinaryOperator::IDiv, { .kind = BKind::HelperCall, .helper = "endo_i64_div" } },
            { BinaryOperator::IRem, { .kind = BKind::HelperCall, .helper = "endo_i64_rem" } },
            { BinaryOperator::IPow, { .kind = BKind::HelperCall, .helper = "endo_i64_pow" } },
            { BinaryOperator::IAnd, { .kind = BKind::DirectOp, .op = &BinaryenAndInt64 } },
            { BinaryOperator::IOr, { .kind = BKind::DirectOp, .op = &BinaryenOrInt64 } },
            { BinaryOperator::IXor, { .kind = BKind::DirectOp, .op = &BinaryenXorInt64 } },
            { BinaryOperator::IShl, { .kind = BKind::DirectOp, .op = &BinaryenShlInt64 } },
            { BinaryOperator::IShr, { .kind = BKind::DirectOp, .op = &BinaryenShrSInt64 } },
            { BinaryOperator::ICmpEQ, { .kind = BKind::DirectOp, .op = &BinaryenEqInt64 } },
            { BinaryOperator::ICmpNE, { .kind = BKind::DirectOp, .op = &BinaryenNeInt64 } },
            { BinaryOperator::ICmpLE, { .kind = BKind::DirectOp, .op = &BinaryenLeSInt64 } },
            { BinaryOperator::ICmpGE, { .kind = BKind::DirectOp, .op = &BinaryenGeSInt64 } },
            { BinaryOperator::ICmpLT, { .kind = BKind::DirectOp, .op = &BinaryenLtSInt64 } },
            { BinaryOperator::ICmpGT, { .kind = BKind::DirectOp, .op = &BinaryenGtSInt64 } },
            // boolean (canonical 0/1 i64 values)
            { BinaryOperator::BAnd, { .kind = BKind::DirectOp, .op = &BinaryenAndInt64 } },
            { BinaryOperator::BOr, { .kind = BKind::DirectOp, .op = &BinaryenOrInt64 } },
            { BinaryOperator::BXor, { .kind = BKind::DirectOp, .op = &BinaryenXorInt64 } },
            // string
            { BinaryOperator::SAdd, { .kind = BKind::HelperCall, .helper = "endo_str_concat" } },
            // SSubStrInstr is never emitted by the frontend (no IRBuilder factory), and
            // its two operands do not match the VM's three-operand SSUBSTR semantics.
            { BinaryOperator::SSubStr,
              { .kind = BKind::Unsupported, .unsupportedWhat = "substring extraction" } },
            { BinaryOperator::SCmpEQ, { .kind = BKind::HelperCall, .helper = "endo_str_eq" } },
            { BinaryOperator::SCmpNE,
              { .kind = BKind::HelperCmp0, .op = &BinaryenEqInt64, .helper = "endo_str_eq" } },
            { BinaryOperator::SCmpLE,
              { .kind = BKind::HelperCmp0, .op = &BinaryenLeSInt64, .helper = "endo_str_cmp" } },
            { BinaryOperator::SCmpGE,
              { .kind = BKind::HelperCmp0, .op = &BinaryenGeSInt64, .helper = "endo_str_cmp" } },
            { BinaryOperator::SCmpLT,
              { .kind = BKind::HelperCmp0, .op = &BinaryenLtSInt64, .helper = "endo_str_cmp" } },
            { BinaryOperator::SCmpGT,
              { .kind = BKind::HelperCmp0, .op = &BinaryenGtSInt64, .helper = "endo_str_cmp" } },
            { BinaryOperator::SCmpRE,
              { .kind = BKind::Unsupported, .unsupportedWhat = "regular expression matching" } },
            { BinaryOperator::SCmpBeg, { .kind = BKind::HelperCall, .helper = "endo_str_starts_with" } },
            { BinaryOperator::SCmpEnd, { .kind = BKind::HelperCall, .helper = "endo_str_ends_with" } },
            { BinaryOperator::SIn, { .kind = BKind::HelperCall, .helper = "endo_str_contains" } },
            // float
            { BinaryOperator::FAdd,
              { .kind = BKind::DirectOp, .op = &BinaryenAddFloat64, .floatOperands = true } },
            { BinaryOperator::FSub,
              { .kind = BKind::DirectOp, .op = &BinaryenSubFloat64, .floatOperands = true } },
            { BinaryOperator::FMul,
              { .kind = BKind::DirectOp, .op = &BinaryenMulFloat64, .floatOperands = true } },
            { BinaryOperator::FDiv,
              { .kind = BKind::DirectOp, .op = &BinaryenDivFloat64, .floatOperands = true } },
            { BinaryOperator::FRem,
              { .kind = BKind::HelperCall, .helper = "endo_f64_rem", .floatOperands = true } },
            { BinaryOperator::FPow,
              { .kind = BKind::HelperCall, .helper = "endo_f64_pow", .floatOperands = true } },
            { BinaryOperator::FCmpEQ,
              { .kind = BKind::DirectOp, .op = &BinaryenEqFloat64, .floatOperands = true } },
            { BinaryOperator::FCmpNE,
              { .kind = BKind::DirectOp, .op = &BinaryenNeFloat64, .floatOperands = true } },
            { BinaryOperator::FCmpLE,
              { .kind = BKind::DirectOp, .op = &BinaryenLeFloat64, .floatOperands = true } },
            { BinaryOperator::FCmpGE,
              { .kind = BKind::DirectOp, .op = &BinaryenGeFloat64, .floatOperands = true } },
            { BinaryOperator::FCmpLT,
              { .kind = BKind::DirectOp, .op = &BinaryenLtFloat64, .floatOperands = true } },
            { BinaryOperator::FCmpGT,
              { .kind = BKind::DirectOp, .op = &BinaryenGtFloat64, .floatOperands = true } },
            // ip
            { BinaryOperator::PCmpEQ,
              { .kind = BKind::Unsupported, .unsupportedWhat = "IP address comparison" } },
            { BinaryOperator::PCmpNE,
              { .kind = BKind::Unsupported, .unsupportedWhat = "IP address comparison" } },
            { BinaryOperator::PInCidr,
              { .kind = BKind::Unsupported, .unsupportedWhat = "CIDR containment test" } },
        };
    }

    std::unordered_map<UnaryOperator, UnaryOpLowering> makeUnaryOpTable()
    {
        return {
            { UnaryOperator::INeg, { .kind = UKind::SubFromZero } },
            { UnaryOperator::INot, { .kind = UKind::XorImmediate, .immediate = -1 } },
            { UnaryOperator::BNot, { .kind = UKind::XorImmediate, .immediate = 1 } },
            { UnaryOperator::FNeg,
              { .kind = UKind::DirectOp, .op = &BinaryenNegFloat64, .floatOperand = true } },
            { UnaryOperator::SLen, { .kind = UKind::HelperCall, .helper = "endo_str_len" } },
            { UnaryOperator::SIsEmpty, { .kind = UKind::HelperIsZero, .helper = "endo_str_len" } },
        };
    }
} // namespace

BinaryOpLowering const& binaryOpLowering(BinaryOperator op)
{
    static auto const table = makeBinaryOpTable();
    static auto const unsupportedFallback =
        BinaryOpLowering { .kind = BKind::Unsupported, .unsupportedWhat = "this operator" };
    auto const it = table.find(op);
    return it != table.end() ? it->second : unsupportedFallback;
}

UnaryOpLowering const& unaryOpLowering(UnaryOperator op)
{
    static auto const table = makeUnaryOpTable();
    static auto const unsupportedFallback =
        UnaryOpLowering { .kind = UKind::Unsupported, .unsupportedWhat = "this operator" };
    auto const it = table.find(op);
    return it != table.end() ? it->second : unsupportedFallback;
}

} // namespace CoreVM::wasm
