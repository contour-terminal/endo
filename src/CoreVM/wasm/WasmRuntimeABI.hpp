// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>

#include <binaryen-c.h>

/// @file
/// The ABI contract between the WASM code generator (WasmCodeGenerator /
/// WasmFunctionLowerer) and the WASM-side runtime implementation
/// (WasmRuntime): linear-memory layout, the unified cell header, and the
/// signatures of all runtime helper functions the generated code may call.

namespace CoreVM::wasm
{

/// Linear-memory layout of a compiled endo module.
///
/// ```
/// 0x0000 .. 0x03FF   null-pointer guard (never written)
/// 0x0400 .. 0x07FF   runtime scratch (iovecs, number formatting)
/// 0x0800 .. dataEnd  data segments (interned string constants)
/// heapBase .. ∞      bump-allocated heap (heapBase = align8(dataEnd))
/// ```
namespace layout
{
    constexpr uint32_t NullGuardSize = 0x0400;       ///< Bytes reserved as null-pointer guard.
    constexpr uint32_t ScratchIovec0 = 0x0400;       ///< First WASI iovec (buf, buf_len).
    constexpr uint32_t ScratchIovec1 = 0x0408;       ///< Second WASI iovec (println newline).
    constexpr uint32_t ScratchNWritten = 0x0410;     ///< fd_write nwritten output slot.
    constexpr uint32_t ScratchNumberBuffer = 0x0440; ///< Number-formatting buffer (512 bytes).
    constexpr uint32_t DataBase = 0x0800;            ///< First byte of the constant data segment.

    /// Every heap or data cell (string or object) starts with this 8-byte header:
    /// byte 0: kind; byte 1: tag; bytes 2-3: typeId (u16 LE);
    /// bytes 4-7 (u32 LE): strings: byte length, objects: refcount (reserved).
    constexpr uint32_t HeaderSize = 8;
    constexpr uint32_t TagOffset = 1;    ///< Sum-type variant tag (u8).
    constexpr uint32_t TypeIdOffset = 2; ///< Object type id (u16 LE).
    constexpr uint32_t LengthOffset = 4; ///< String byte length (u32 LE).
    constexpr uint32_t SlotSize = 8;     ///< Object slot width in bytes.
    constexpr uint8_t KindString = 0xE5; ///< Header kind byte for strings.
    constexpr uint8_t KindObject = 0xE6; ///< Header kind byte for objects.

    constexpr std::string_view MemoryExportName = "memory";           ///< WASI requires this export.
    constexpr std::string_view HeapPointerGlobal = "endo_heap_ptr";   ///< Mutable i32 bump pointer.
    constexpr std::string_view ExitStatusGlobal = "endo_exit_status"; ///< Mutable i32 exit status.
} // namespace layout

/// WASM value type in runtime-helper signatures. This is a constexpr-friendly
/// mirror of BinaryenType, whose values are only obtainable via function calls.
enum class ValType : uint8_t
{
    I32,
    I64,
    F64,
};

/// Converts a ValType to the corresponding BinaryenType.
[[nodiscard]] inline BinaryenType toBinaryenType(ValType type)
{
    switch (type)
    {
        case ValType::I32: return BinaryenTypeInt32();
        case ValType::I64: return BinaryenTypeInt64();
        case ValType::F64: return BinaryenTypeFloat64();
    }
    return BinaryenTypeNone();
}

/// Signature descriptor of one in-module runtime helper function.
///
/// All VM-visible values use the uniform i64 representation (numbers as-is,
/// floats bit-cast, booleans 0/1, string/object pointers zero-extended), so
/// value-level parameters and results are ValType::I64.
struct RuntimeHelperDef
{
    std::string_view name;           ///< Function name inside the module, e.g. "endo_str_concat".
    std::span<ValType const> params; ///< Parameter types.
    std::optional<ValType> result;   ///< Result type; nullopt for no result.
    bool noReturn = false;           ///< True if the helper never returns (traps/exits).
};

namespace detail
{
    inline constexpr auto I64x1 = std::to_array({ ValType::I64 });
    inline constexpr auto I64x2 = std::to_array({ ValType::I64, ValType::I64 });
    inline constexpr auto I64x3 = std::to_array({ ValType::I64, ValType::I64, ValType::I64 });
    inline constexpr auto F64x1 = std::to_array({ ValType::F64 });
    inline constexpr auto F64x2 = std::to_array({ ValType::F64, ValType::F64 });
} // namespace detail

/// All runtime helpers callable from generated code. This table is the single
/// source of truth: the code generator emits calls against these signatures
/// and the runtime provider (real or import-only) materializes matching
/// functions. Adding a helper means adding a row here plus a body builder in
/// WasmRuntime.
inline constexpr auto RuntimeHelpers = std::to_array<RuntimeHelperDef>({
    // integer math
    { .name = "endo_i64_div", .params = detail::I64x2, .result = ValType::I64 },
    { .name = "endo_i64_rem", .params = detail::I64x2, .result = ValType::I64 },
    { .name = "endo_i64_pow", .params = detail::I64x2, .result = ValType::I64 },
    // float math
    { .name = "endo_f64_rem", .params = detail::F64x2, .result = ValType::F64 },
    { .name = "endo_f64_pow", .params = detail::F64x2, .result = ValType::F64 },
    // strings
    { .name = "endo_str_concat", .params = detail::I64x2, .result = ValType::I64 },
    { .name = "endo_str_substr", .params = detail::I64x3, .result = ValType::I64 },
    { .name = "endo_str_len", .params = detail::I64x1, .result = ValType::I64 },
    { .name = "endo_str_eq", .params = detail::I64x2, .result = ValType::I64 },
    { .name = "endo_str_cmp", .params = detail::I64x2, .result = ValType::I64 },
    { .name = "endo_str_starts_with", .params = detail::I64x2, .result = ValType::I64 },
    { .name = "endo_str_ends_with", .params = detail::I64x2, .result = ValType::I64 },
    { .name = "endo_str_contains", .params = detail::I64x2, .result = ValType::I64 },
    // number <-> string conversion
    { .name = "endo_i64_to_str", .params = detail::I64x1, .result = ValType::I64 },
    { .name = "endo_f64_to_str_g", .params = detail::F64x1, .result = ValType::I64 },
    { .name = "endo_f64_to_str_fixed", .params = detail::F64x1, .result = ValType::I64 },
    { .name = "endo_str_to_i64", .params = detail::I64x1, .result = ValType::I64 },
    { .name = "endo_str_to_f64", .params = detail::I64x1, .result = ValType::F64 },
    // objects
    { .name = "endo_obj_alloc", .params = detail::I64x2, .result = ValType::I64 },
    // lists
    { .name = "endo_list_length", .params = detail::I64x1, .result = ValType::I64 },
    { .name = "endo_list_is_empty", .params = detail::I64x1, .result = ValType::I64 },
    { .name = "endo_list_head", .params = detail::I64x1, .result = ValType::I64 },
    { .name = "endo_list_tail", .params = detail::I64x1, .result = ValType::I64 },
    { .name = "endo_list_nth", .params = detail::I64x2, .result = ValType::I64 },
    { .name = "endo_list_concat", .params = detail::I64x2, .result = ValType::I64 },
    // value formatting (composite display)
    { .name = "endo_object_to_string", .params = detail::I64x1, .result = ValType::I64 },
    { .name = "endo_list_to_string", .params = detail::I64x1, .result = ValType::I64 },
    { .name = "endo_value_to_str", .params = detail::I64x1, .result = ValType::I64 },
    // I/O
    { .name = "endo_print", .params = detail::I64x1, .result = std::nullopt },
    { .name = "endo_println", .params = detail::I64x1, .result = std::nullopt },
    { .name = "endo_display_result", .params = detail::I64x1, .result = std::nullopt },
    { .name = "endo_panic", .params = detail::I64x1, .result = std::nullopt, .noReturn = true },
});

/// Looks up a runtime helper by name.
/// @return the descriptor, or nullptr if no such helper exists.
[[nodiscard]] inline RuntimeHelperDef const* findRuntimeHelper(std::string_view name)
{
    for (auto const& helper: RuntimeHelpers)
        if (helper.name == name)
            return &helper;
    return nullptr;
}

/// Builds the BinaryenType tuple for a helper's parameter list.
[[nodiscard]] inline BinaryenType binaryenParamsType(RuntimeHelperDef const& def)
{
    auto types = std::array<BinaryenType, 8> {};
    auto count = size_t { 0 };
    for (auto const param: def.params)
        types.at(count++) = toBinaryenType(param);
    return BinaryenTypeCreate(types.data(), static_cast<BinaryenIndex>(count));
}

/// Builds the BinaryenType for a helper's result.
[[nodiscard]] inline BinaryenType binaryenResultType(RuntimeHelperDef const& def)
{
    return def.result ? toBinaryenType(*def.result) : BinaryenTypeNone();
}

class WasmStringTable;

/// Dependency-injection seam between the code generator and the WASM-side
/// runtime: the generator lowers user code and records which runtime helpers
/// it referenced; the provider then materializes those helpers into the module.
class WasmRuntimeProvider
{
  public:
    virtual ~WasmRuntimeProvider() = default;

    /// Materializes all used runtime helpers into the module.
    ///
    /// Called once per module, after all user code has been lowered and
    /// before the memory layout is finalized (providers may intern further
    /// strings, e.g. diagnostic messages).
    ///
    /// @param module      the module under construction
    /// @param usedHelpers helpers referenced by the generated code
    /// @param strings     the module's string constant table
    virtual void provide(BinaryenModuleRef module,
                         std::set<RuntimeHelperDef const*> const& usedHelpers,
                         WasmStringTable& strings) = 0;
};

/// Declares every used helper as a function import from module "endo".
///
/// The resulting module validates and its WAT is inspectable, but it cannot
/// run standalone (no host provides the "endo" imports). Used by unit tests
/// and early development milestones.
class ImportOnlyRuntimeProvider final: public WasmRuntimeProvider
{
  public:
    void provide(BinaryenModuleRef module,
                 std::set<RuntimeHelperDef const*> const& usedHelpers,
                 WasmStringTable& /*strings*/) override
    {
        for (auto const* helper: usedHelpers)
        {
            auto const name = std::string(helper->name);
            BinaryenAddFunctionImport(module,
                                      name.c_str(),
                                      "endo",
                                      name.c_str(),
                                      binaryenParamsType(*helper),
                                      binaryenResultType(*helper));
        }
    }
};

} // namespace CoreVM::wasm
