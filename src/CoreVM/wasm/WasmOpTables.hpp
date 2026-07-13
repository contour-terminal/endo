// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <CoreVM/enums.hpp>

#include <cstdint>
#include <string_view>

#include <binaryen-c.h>

/// @file
/// Data-driven lowering tables for the IR operator families (I*, B*, S*, F*,
/// P*). Adding or changing an operator lowering means changing a table row,
/// not writing new visitor logic.

namespace CoreVM::wasm
{

/// How one IR binary operator lowers to WASM.
struct BinaryOpLowering
{
    enum class Kind : uint8_t
    {
        DirectOp,    ///< One binaryen instruction (op).
        HelperCall,  ///< Call a runtime helper (helper).
        HelperCmp0,  ///< helper(lhs, rhs) <op> 0 — for three-way comparison helpers.
        Unsupported, ///< Clean compile-time error (unsupportedWhat).
    };

    Kind kind = Kind::Unsupported;
    BinaryenOp (*op)() = nullptr;     ///< DirectOp: the instruction; HelperCmp0: the comparison.
    std::string_view helper;          ///< HelperCall/HelperCmp0: runtime helper name.
    bool floatOperands = false;       ///< Coerce operands to f64 instead of i64.
    std::string_view unsupportedWhat; ///< Unsupported: user-facing description.
};

/// How one IR unary operator lowers to WASM.
struct UnaryOpLowering
{
    enum class Kind : uint8_t
    {
        DirectOp,     ///< One binaryen unary instruction (op).
        SubFromZero,  ///< i64.sub(0, x) — integer negation.
        XorImmediate, ///< i64.xor(x, immediate) — bitwise/boolean not.
        HelperCall,   ///< Call a runtime helper (helper).
        HelperIsZero, ///< i64.eqz(helper(x)) — e.g. string emptiness via length.
        Unsupported,  ///< Clean compile-time error (unsupportedWhat).
    };

    Kind kind = Kind::Unsupported;
    BinaryenOp (*op)() = nullptr;     ///< DirectOp: the instruction.
    std::string_view helper;          ///< HelperCall/HelperIsZero: runtime helper name.
    int64_t immediate = 0;            ///< XorImmediate: the constant.
    bool floatOperand = false;        ///< Coerce the operand to f64 instead of i64.
    std::string_view unsupportedWhat; ///< Unsupported: user-facing description.
};

/// How one (target, source) type conversion lowers to WASM. Mirrors
/// TargetCodeGenerator's cast matrix: dynamic types (Void/Object) are treated
/// as numbers at runtime.
struct CastLowering
{
    enum class Kind : uint8_t
    {
        HelperI64, ///< Runtime helper taking the canonical i64 form.
        HelperF64, ///< Runtime helper taking the f64 view.
        TruncSat,  ///< f64 -> i64 (non-trapping saturating truncation).
        Convert,   ///< i64 -> f64 numeric conversion.
    };

    LiteralType target;
    LiteralType source;
    Kind kind;
    std::string_view helper; ///< HelperI64/HelperF64: runtime helper name.
};

/// Looks up the lowering for a binary operator. Never returns nullptr.
[[nodiscard]] BinaryOpLowering const& binaryOpLowering(BinaryOperator op);

/// Looks up the lowering for a unary operator. Never returns nullptr.
[[nodiscard]] UnaryOpLowering const& unaryOpLowering(UnaryOperator op);

/// Looks up the lowering for a type conversion.
/// @return the matching row, or nullptr if the conversion is unsupported.
[[nodiscard]] CastLowering const* castLowering(LiteralType target, LiteralType source);

} // namespace CoreVM::wasm
