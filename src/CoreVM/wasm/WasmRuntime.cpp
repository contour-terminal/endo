// SPDX-License-Identifier: Apache-2.0
#include <CoreVM/wasm/WasmRuntime.hpp>
#include <CoreVM/wasm/WasmStringTable.hpp>

#include <array>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include <binaryen-c.h>

namespace CoreVM::wasm
{

namespace
{
    /// Terse expression-building facade over the binaryen C API, used to keep
    /// the runtime function bodies readable. All member functions build fresh
    /// expression nodes (binaryen expressions are single-use tree nodes).
    struct Emit
    {
        BinaryenModuleRef m;

        // constants
        [[nodiscard]] BinaryenExpressionRef i32(int32_t v) const
        {
            return BinaryenConst(m, BinaryenLiteralInt32(v));
        }

        [[nodiscard]] BinaryenExpressionRef i64(int64_t v) const
        {
            return BinaryenConst(m, BinaryenLiteralInt64(v));
        }

        // locals (indices count parameters first)
        [[nodiscard]] BinaryenExpressionRef get32(BinaryenIndex i) const
        {
            return BinaryenLocalGet(m, i, BinaryenTypeInt32());
        }

        [[nodiscard]] BinaryenExpressionRef get64(BinaryenIndex i) const
        {
            return BinaryenLocalGet(m, i, BinaryenTypeInt64());
        }

        [[nodiscard]] BinaryenExpressionRef set(BinaryenIndex i, BinaryenExpressionRef v) const
        {
            return BinaryenLocalSet(m, i, v);
        }

        // integer narrowing/widening
        [[nodiscard]] BinaryenExpressionRef wrap(BinaryenExpressionRef v) const
        {
            return un(BinaryenWrapInt64(), v);
        }

        [[nodiscard]] BinaryenExpressionRef extendU(BinaryenExpressionRef v) const
        {
            return un(BinaryenExtendUInt32(), v);
        }

        [[nodiscard]] BinaryenExpressionRef f64(double v) const
        {
            return BinaryenConst(m, BinaryenLiteralFloat64(v));
        }

        [[nodiscard]] BinaryenExpressionRef getF64(BinaryenIndex i) const
        {
            return BinaryenLocalGet(m, i, BinaryenTypeFloat64());
        }

        // memory (32-bit addresses)
        [[nodiscard]] BinaryenExpressionRef load8u(BinaryenExpressionRef ptr, uint32_t offset = 0) const
        {
            return BinaryenLoad(m, 1, false, offset, 0, BinaryenTypeInt32(), ptr, "0");
        }

        [[nodiscard]] BinaryenExpressionRef load16u(BinaryenExpressionRef ptr, uint32_t offset = 0) const
        {
            return BinaryenLoad(m, 2, false, offset, 0, BinaryenTypeInt32(), ptr, "0");
        }

        [[nodiscard]] BinaryenExpressionRef load64(BinaryenExpressionRef ptr, uint32_t offset = 0) const
        {
            return BinaryenLoad(m, 8, false, offset, 0, BinaryenTypeInt64(), ptr, "0");
        }

        [[nodiscard]] BinaryenExpressionRef store16(BinaryenExpressionRef ptr,
                                                    BinaryenExpressionRef v,
                                                    uint32_t offset = 0) const
        {
            return BinaryenStore(m, 2, offset, 0, ptr, v, BinaryenTypeInt32(), "0");
        }

        [[nodiscard]] BinaryenExpressionRef store64(BinaryenExpressionRef ptr,
                                                    BinaryenExpressionRef v,
                                                    uint32_t offset = 0) const
        {
            return BinaryenStore(m, 8, offset, 0, ptr, v, BinaryenTypeInt64(), "0");
        }

        [[nodiscard]] BinaryenExpressionRef load32(BinaryenExpressionRef ptr, uint32_t offset = 0) const
        {
            return BinaryenLoad(m, 4, false, offset, 0, BinaryenTypeInt32(), ptr, "0");
        }

        [[nodiscard]] BinaryenExpressionRef store8(BinaryenExpressionRef ptr,
                                                   BinaryenExpressionRef v,
                                                   uint32_t offset = 0) const
        {
            return BinaryenStore(m, 1, offset, 0, ptr, v, BinaryenTypeInt32(), "0");
        }

        [[nodiscard]] BinaryenExpressionRef store32(BinaryenExpressionRef ptr,
                                                    BinaryenExpressionRef v,
                                                    uint32_t offset = 0) const
        {
            return BinaryenStore(m, 4, offset, 0, ptr, v, BinaryenTypeInt32(), "0");
        }

        // operators
        [[nodiscard]] BinaryenExpressionRef un(BinaryenOp op, BinaryenExpressionRef v) const
        {
            return BinaryenUnary(m, op, v);
        }

        [[nodiscard]] BinaryenExpressionRef bin(BinaryenOp op,
                                                BinaryenExpressionRef l,
                                                BinaryenExpressionRef r) const
        {
            return BinaryenBinary(m, op, l, r);
        }

        // control flow
        [[nodiscard]] BinaryenExpressionRef block(std::vector<BinaryenExpressionRef> stmts,
                                                  char const* name = nullptr) const
        {
            return BinaryenBlock(
                m, name, stmts.data(), static_cast<BinaryenIndex>(stmts.size()), BinaryenTypeAuto());
        }

        [[nodiscard]] BinaryenExpressionRef loop(char const* name, BinaryenExpressionRef body) const
        {
            return BinaryenLoop(m, name, body);
        }

        [[nodiscard]] BinaryenExpressionRef brIf(char const* label, BinaryenExpressionRef condition) const
        {
            return BinaryenBreak(m, label, condition, nullptr);
        }

        [[nodiscard]] BinaryenExpressionRef br(char const* label) const
        {
            return BinaryenBreak(m, label, nullptr, nullptr);
        }

        [[nodiscard]] BinaryenExpressionRef ifThen(BinaryenExpressionRef cond,
                                                   BinaryenExpressionRef then) const
        {
            return BinaryenIf(m, cond, then, nullptr);
        }

        [[nodiscard]] BinaryenExpressionRef ifElse(BinaryenExpressionRef cond,
                                                   BinaryenExpressionRef then,
                                                   BinaryenExpressionRef otherwise) const
        {
            return BinaryenIf(m, cond, then, otherwise);
        }

        [[nodiscard]] BinaryenExpressionRef ret(BinaryenExpressionRef v = nullptr) const
        {
            return BinaryenReturn(m, v);
        }

        [[nodiscard]] BinaryenExpressionRef call(char const* target,
                                                 std::vector<BinaryenExpressionRef> args,
                                                 BinaryenType resultType) const
        {
            return BinaryenCall(m, target, args.data(), static_cast<BinaryenIndex>(args.size()), resultType);
        }

        [[nodiscard]] BinaryenExpressionRef callVoid(char const* target,
                                                     std::vector<BinaryenExpressionRef> args) const
        {
            return call(target, std::move(args), BinaryenTypeNone());
        }

        [[nodiscard]] BinaryenExpressionRef heapPtr() const
        {
            return BinaryenGlobalGet(m, std::string(layout::HeapPointerGlobal).c_str(), BinaryenTypeInt32());
        }

        [[nodiscard]] BinaryenExpressionRef setHeapPtr(BinaryenExpressionRef v) const
        {
            return BinaryenGlobalSet(m, std::string(layout::HeapPointerGlobal).c_str(), v);
        }

        void addFunction(char const* name,
                         std::vector<BinaryenType> params,
                         BinaryenType result,
                         std::vector<BinaryenType> locals,
                         BinaryenExpressionRef body) const
        {
            auto const paramsType =
                params.empty() ? BinaryenTypeNone()
                               : BinaryenTypeCreate(params.data(), static_cast<BinaryenIndex>(params.size()));
            BinaryenAddFunction(
                m, name, paramsType, result, locals.data(), static_cast<BinaryenIndex>(locals.size()), body);
        }
    };

    constexpr auto I32 = BinaryenTypeInt32;
    constexpr auto I64 = BinaryenTypeInt64;
} // namespace

void WasmRuntime::provide(BinaryenModuleRef module,
                          std::set<RuntimeHelperDef const*> const& usedHelpers,
                          WasmStringTable& strings)
{
    _module = module;
    _strings = &strings;
    for (auto const* helper: usedHelpers)
        require(helper->name);
}

void WasmRuntime::require(std::string_view name)
{
    if (_built.contains(name))
        return;
    _built.emplace(name);

    using Builder = void (WasmRuntime::*)();
    static auto const builders = std::unordered_map<std::string_view, Builder> {
        { "endo_alloc", &WasmRuntime::buildAlloc },
        { "endo_str_alloc", &WasmRuntime::buildStrAlloc },
        { "endo_write_all", &WasmRuntime::buildWriteAll },
        { "endo_print", &WasmRuntime::buildPrint },
        { "endo_println", &WasmRuntime::buildPrintln },
        { "endo_panic", &WasmRuntime::buildPanic },
        { "endo_i64_to_str", &WasmRuntime::buildI64ToStr },
        { "endo_object_to_string", &WasmRuntime::buildObjectToString },
        { "endo_i64_div", &WasmRuntime::buildI64Div },
        { "endo_i64_rem", &WasmRuntime::buildI64Rem },
        { "endo_i64_pow", &WasmRuntime::buildI64Pow },
        { "endo_mem_eq", &WasmRuntime::buildMemEq },
        { "endo_str_concat", &WasmRuntime::buildStrConcat },
        { "endo_str_len", &WasmRuntime::buildStrLen },
        { "endo_str_eq", &WasmRuntime::buildStrEq },
        { "endo_str_cmp", &WasmRuntime::buildStrCmp },
        { "endo_str_starts_with", &WasmRuntime::buildStrStartsWith },
        { "endo_str_ends_with", &WasmRuntime::buildStrEndsWith },
        { "endo_str_contains", &WasmRuntime::buildStrContains },
        { "endo_str_to_i64", &WasmRuntime::buildStrToI64 },
        { "endo_str_slice", &WasmRuntime::buildStrSlice },
        { "endo_obj_alloc", &WasmRuntime::buildObjAlloc },
        { "endo_f64_to_str_fixed", &WasmRuntime::buildF64ToStrFixed },
        { "endo_slot_to_str", &WasmRuntime::buildSlotToStr },
        { "endo_value_to_str", &WasmRuntime::buildValueToStr },
        { "endo_list_to_string", &WasmRuntime::buildListToString },
        { "endo_option_str", &WasmRuntime::buildOptionStr },
        { "endo_result_str", &WasmRuntime::buildResultStr },
        { "endo_tuple2_str", &WasmRuntime::buildTuple2Str },
        { "endo_tuple3_str", &WasmRuntime::buildTuple3Str },
    };

    if (auto const it = builders.find(name); it != builders.end())
        (this->*(it->second))();
    else
        importHelper(name);
}

void WasmRuntime::requireFdWrite()
{
    if (_built.contains("fd_write"))
        return;
    _built.emplace("fd_write");
    auto params = std::array { I32(), I32(), I32(), I32() };
    BinaryenAddFunctionImport(_module,
                              "fd_write",
                              "wasi_snapshot_preview1",
                              "fd_write",
                              BinaryenTypeCreate(params.data(), static_cast<BinaryenIndex>(params.size())),
                              I32());
}

void WasmRuntime::importHelper(std::string_view name)
{
    auto const* def = findRuntimeHelper(name);
    if (def == nullptr)
        return; // unknown internal name: leave for module validation to catch
    auto const functionName = std::string(name);
    BinaryenAddFunctionImport(_module,
                              functionName.c_str(),
                              "endo",
                              functionName.c_str(),
                              binaryenParamsType(*def),
                              binaryenResultType(*def));
}

// endo_alloc(size: i32) -> i32 — bump allocation with memory.grow on demand.
void WasmRuntime::buildAlloc()
{
    require("endo_panic");
    auto e = Emit { _module };
    auto const oom = static_cast<int64_t>(_strings->intern("out of memory"));

    // params: 0=size; locals: 1=ptr, 2=newPtr, 3=pages
    auto* body = e.block({
        // size = (size + 7) & ~7
        e.set(0, e.bin(BinaryenAndInt32(), e.bin(BinaryenAddInt32(), e.get32(0), e.i32(7)), e.i32(~7))),
        e.set(1, e.heapPtr()),
        e.set(2, e.bin(BinaryenAddInt32(), e.get32(1), e.get32(0))),
        // if (newPtr > memory.size << 16) grow
        e.ifThen(
            e.bin(BinaryenGtUInt32(),
                  e.get32(2),
                  e.bin(BinaryenShlInt32(), BinaryenMemorySize(_module, "0", false), e.i32(16))),
            e.block({
                // pages = ((newPtr - (memory.size << 16)) >> 16) + 1, at least 16
                e.set(3,
                      e.bin(BinaryenAddInt32(),
                            e.bin(BinaryenShrUInt32(),
                                  e.bin(BinaryenSubInt32(),
                                        e.get32(2),
                                        e.bin(BinaryenShlInt32(),
                                              BinaryenMemorySize(_module, "0", false),
                                              e.i32(16))),
                                  e.i32(16)),
                            e.i32(1))),
                e.ifThen(e.bin(BinaryenLtUInt32(), e.get32(3), e.i32(16)), e.set(3, e.i32(16))),
                e.ifThen(
                    e.bin(BinaryenEqInt32(), BinaryenMemoryGrow(_module, e.get32(3), "0", false), e.i32(-1)),
                    e.callVoid("endo_panic", { e.i64(oom) })),
            })),
        e.setHeapPtr(e.get32(2)),
        e.ret(e.get32(1)),
    });

    e.addFunction("endo_alloc", { I32() }, I32(), { I32(), I32(), I32() }, body);
}

// endo_str_alloc(len: i32) -> i32 — allocates a string cell (header + payload).
void WasmRuntime::buildStrAlloc()
{
    require("endo_alloc");
    auto e = Emit { _module };

    // params: 0=len; locals: 1=p
    auto* body = e.block({
        e.set(
            1,
            e.call("endo_alloc",
                   { e.bin(BinaryenAddInt32(), e.i32(static_cast<int32_t>(layout::HeaderSize)), e.get32(0)) },
                   I32())),
        e.store8(e.get32(1), e.i32(layout::KindString)),
        e.store32(e.get32(1), e.get32(0), layout::LengthOffset),
        e.ret(e.get32(1)),
    });

    e.addFunction("endo_str_alloc", { I32() }, I32(), { I32() }, body);
}

// endo_write_all(fd: i32, buf: i32, len: i32) — fd_write loop handling
// partial writes; write errors are silently dropped (matching VM print).
void WasmRuntime::buildWriteAll()
{
    requireFdWrite();
    auto e = Emit { _module };
    auto const iovec = static_cast<int32_t>(layout::ScratchIovec0);
    auto const nwritten = static_cast<int32_t>(layout::ScratchNWritten);

    // params: 0=fd, 1=buf, 2=len; locals: 3=n
    auto* body = e.block(
        { e.loop(
            "write",
            e.block({
                e.brIf("done", e.un(BinaryenEqZInt32(), e.get32(2))),
                e.store32(e.i32(iovec), e.get32(1)),
                e.store32(e.i32(iovec), e.get32(2), 4),
                e.brIf("done",
                       e.call("fd_write", { e.get32(0), e.i32(iovec), e.i32(1), e.i32(nwritten) }, I32())),
                e.set(3, e.load32(e.i32(nwritten))),
                e.brIf("done", e.un(BinaryenEqZInt32(), e.get32(3))),
                e.set(1, e.bin(BinaryenAddInt32(), e.get32(1), e.get32(3))),
                e.set(2, e.bin(BinaryenSubInt32(), e.get32(2), e.get32(3))),
                e.br("write"),
            })) },
        "done");

    e.addFunction("endo_write_all", { I32(), I32(), I32() }, BinaryenTypeNone(), { I32() }, body);
}

// endo_print(s: i64) — writes the string bytes to stdout.
void WasmRuntime::buildPrint()
{
    require("endo_write_all");
    auto e = Emit { _module };

    // params: 0=s; locals: 1=p
    auto* body = e.block({
        e.set(1, e.wrap(e.get64(0))),
        e.callVoid("endo_write_all",
                   { e.i32(1),
                     e.bin(BinaryenAddInt32(), e.get32(1), e.i32(static_cast<int32_t>(layout::HeaderSize))),
                     e.load32(e.get32(1), layout::LengthOffset) }),
    });

    e.addFunction("endo_print", { I64() }, BinaryenTypeNone(), { I32() }, body);
}

// endo_println(s: i64) — print followed by a newline.
void WasmRuntime::buildPrintln()
{
    require("endo_print");
    auto e = Emit { _module };
    auto const newline = static_cast<int32_t>(_strings->intern("\n"));

    auto* body = e.block({
        e.callVoid("endo_print", { e.get64(0) }),
        e.callVoid("endo_write_all",
                   { e.i32(1), e.i32(newline + static_cast<int32_t>(layout::HeaderSize)), e.i32(1) }),
    });

    e.addFunction("endo_println", { I64() }, BinaryenTypeNone(), {}, body);
}

// endo_panic(msg: i64) — "endo: runtime error: <msg>\n" to stderr, exit 1.
void WasmRuntime::buildPanic()
{
    require("endo_write_all");
    auto e = Emit { _module };
    auto const prefixText = std::string_view { "endo: runtime error: " };
    auto const prefix = static_cast<int32_t>(_strings->intern(prefixText));
    auto const newline = static_cast<int32_t>(_strings->intern("\n"));
    auto const headerSize = static_cast<int32_t>(layout::HeaderSize);

    // params: 0=msg; locals: 1=p
    auto* body = e.block({
        e.callVoid("endo_write_all",
                   { e.i32(2), e.i32(prefix + headerSize), e.i32(static_cast<int32_t>(prefixText.size())) }),
        e.set(1, e.wrap(e.get64(0))),
        e.callVoid("endo_write_all",
                   { e.i32(2),
                     e.bin(BinaryenAddInt32(), e.get32(1), e.i32(headerSize)),
                     e.load32(e.get32(1), layout::LengthOffset) }),
        e.callVoid("endo_write_all", { e.i32(2), e.i32(newline + headerSize), e.i32(1) }),
        e.callVoid("proc_exit", { e.i32(1) }),
        BinaryenUnreachable(_module),
    });

    e.addFunction("endo_panic", { I64() }, BinaryenTypeNone(), { I32() }, body);
}

// endo_i64_to_str(v: i64) -> i64 — decimal formatting, INT64_MIN-safe
// (digits are produced in negative space).
void WasmRuntime::buildI64ToStr()
{
    require("endo_str_alloc");
    auto e = Emit { _module };
    auto const bufEnd = static_cast<int32_t>(layout::ScratchNumberBuffer) + 40;

    // params: 0=v; locals: 1=pos i32, 2=neg i32, 3=str i32, 4=len i32
    auto* digitsLoop = e.block(
        { e.loop("digits",
                 e.block({
                     e.brIf("digits.done", e.un(BinaryenEqZInt64(), e.get64(0))),
                     e.set(1, e.bin(BinaryenSubInt32(), e.get32(1), e.i32(1))),
                     // *pos = '0' + (0 - (v % 10))   [v <= 0, so v%10 in (-9..0]]
                     e.store8(e.get32(1),
                              e.bin(BinaryenAddInt32(),
                                    e.i32('0'),
                                    e.wrap(e.bin(BinaryenSubInt64(),
                                                 e.i64(0),
                                                 e.bin(BinaryenRemSInt64(), e.get64(0), e.i64(10)))))),
                     e.set(0, e.bin(BinaryenDivSInt64(), e.get64(0), e.i64(10))),
                     e.br("digits"),
                 })) },
        "digits.done");

    auto* body = e.block({
        e.set(1, e.i32(bufEnd)),
        // neg = v < 0; then continue with v <= 0 (v = neg ? v : -v)
        e.set(2, e.bin(BinaryenLtSInt64(), e.get64(0), e.i64(0))),
        e.ifThen(e.un(BinaryenEqZInt32(), e.get32(2)),
                 e.set(0, e.bin(BinaryenSubInt64(), e.i64(0), e.get64(0)))),
        e.ifElse(e.un(BinaryenEqZInt64(), e.get64(0)),
                 e.block({
                     e.set(1, e.bin(BinaryenSubInt32(), e.get32(1), e.i32(1))),
                     e.store8(e.get32(1), e.i32('0')),
                 }),
                 e.block({
                     digitsLoop,
                     e.ifThen(e.get32(2),
                              e.block({
                                  e.set(1, e.bin(BinaryenSubInt32(), e.get32(1), e.i32(1))),
                                  e.store8(e.get32(1), e.i32('-')),
                              })),
                 })),
        e.set(4, e.bin(BinaryenSubInt32(), e.i32(bufEnd), e.get32(1))),
        e.set(3, e.call("endo_str_alloc", { e.get32(4) }, I32())),
        BinaryenMemoryCopy(
            _module,
            e.bin(BinaryenAddInt32(), e.get32(3), e.i32(static_cast<int32_t>(layout::HeaderSize))),
            e.get32(1),
            e.get32(4),
            "0",
            "0"),
        e.ret(e.extendU(e.get32(3))),
    });

    e.addFunction("endo_i64_to_str", { I64() }, I64(), { I32(), I32(), I32(), I32() }, body);
}

// endo_object_to_string(v: i64) -> i64 — public entry for the builtin
// object_to_string(I)S: delegates to the recursive value classifier.
void WasmRuntime::buildObjectToString()
{
    require("endo_value_to_str");
    auto e = Emit { _module };
    auto* body = e.ret(e.call("endo_value_to_str", { e.get64(0) }, I64()));
    e.addFunction("endo_object_to_string", { I64() }, I64(), {}, body);
}

// endo_i64_div(a, b) -> i64 — VM semantics: division by zero is a runtime
// error; INT64_MIN / -1 yields INT64_MIN instead of trapping.
void WasmRuntime::buildI64Div()
{
    require("endo_panic");
    auto e = Emit { _module };
    auto const divByZero = static_cast<int64_t>(_strings->intern("division by zero"));

    auto* body = e.block({
        e.ifThen(e.un(BinaryenEqZInt64(), e.get64(1)), e.callVoid("endo_panic", { e.i64(divByZero) })),
        e.ifThen(e.bin(BinaryenAndInt32(),
                       e.bin(BinaryenEqInt64(), e.get64(0), e.i64(INT64_MIN)),
                       e.bin(BinaryenEqInt64(), e.get64(1), e.i64(-1))),
                 e.ret(e.i64(INT64_MIN))),
        e.ret(e.bin(BinaryenDivSInt64(), e.get64(0), e.get64(1))),
    });

    e.addFunction("endo_i64_div", { I64(), I64() }, I64(), {}, body);
}

// endo_i64_rem(a, b) -> i64 — VM semantics: remainder by zero is a runtime
// error. (i64.rem_s never traps on INT64_MIN % -1; it yields 0.)
void WasmRuntime::buildI64Rem()
{
    require("endo_panic");
    auto e = Emit { _module };
    auto const divByZero = static_cast<int64_t>(_strings->intern("division by zero"));

    auto* body = e.block({
        e.ifThen(e.un(BinaryenEqZInt64(), e.get64(1)), e.callVoid("endo_panic", { e.i64(divByZero) })),
        e.ret(e.bin(BinaryenRemSInt64(), e.get64(0), e.get64(1))),
    });

    e.addFunction("endo_i64_rem", { I64(), I64() }, I64(), {}, body);
}

// endo_i64_pow(base, exp) -> i64 — square-and-multiply; negative exponents
// follow integer-power semantics (only |base| == 1 yields non-zero).
void WasmRuntime::buildI64Pow()
{
    auto e = Emit { _module };

    // params: 0=base, 1=exp; locals: 2=result
    auto* body = e.block({
        e.ifThen(e.bin(BinaryenLtSInt64(), e.get64(1), e.i64(0)),
                 e.block({
                     e.ifThen(e.bin(BinaryenEqInt64(), e.get64(0), e.i64(1)), e.ret(e.i64(1))),
                     e.ifThen(e.bin(BinaryenEqInt64(), e.get64(0), e.i64(-1)),
                              e.ret(e.bin(BinaryenSubInt64(),
                                          e.i64(1),
                                          e.bin(BinaryenShlInt64(),
                                                e.bin(BinaryenAndInt64(), e.get64(1), e.i64(1)),
                                                e.i64(1))))),
                     e.ret(e.i64(0)),
                 })),
        e.set(2, e.i64(1)),
        e.block({ e.loop("pow",
                         e.block({
                             e.brIf("pow.done", e.un(BinaryenEqZInt64(), e.get64(1))),
                             e.ifThen(e.wrap(e.bin(BinaryenAndInt64(), e.get64(1), e.i64(1))),
                                      e.set(2, e.bin(BinaryenMulInt64(), e.get64(2), e.get64(0)))),
                             e.set(0, e.bin(BinaryenMulInt64(), e.get64(0), e.get64(0))),
                             e.set(1, e.bin(BinaryenShrUInt64(), e.get64(1), e.i64(1))),
                             e.br("pow"),
                         })) },
                "pow.done"),
        e.ret(e.get64(2)),
    });

    e.addFunction("endo_i64_pow", { I64(), I64() }, I64(), { I64() }, body);
}

// endo_mem_eq(a: i32, b: i32, n: i32) -> i32 — byte-wise memory equality.
void WasmRuntime::buildMemEq()
{
    auto e = Emit { _module };

    // params: 0=a, 1=b, 2=n; locals: 3=i
    auto* body =
        e.block({ e.loop("cmp",
                         e.block({
                             e.ifThen(e.bin(BinaryenGeUInt32(), e.get32(3), e.get32(2)), e.ret(e.i32(1))),
                             e.ifThen(e.bin(BinaryenNeInt32(),
                                            e.load8u(e.bin(BinaryenAddInt32(), e.get32(0), e.get32(3))),
                                            e.load8u(e.bin(BinaryenAddInt32(), e.get32(1), e.get32(3)))),
                                      e.ret(e.i32(0))),
                             e.set(3, e.bin(BinaryenAddInt32(), e.get32(3), e.i32(1))),
                             e.br("cmp"),
                         })) });

    e.addFunction("endo_mem_eq", { I32(), I32(), I32() }, I32(), { I32() }, body);
}

// endo_str_concat(a: i64, b: i64) -> i64
void WasmRuntime::buildStrConcat()
{
    require("endo_str_alloc");
    auto e = Emit { _module };
    auto const headerSize = static_cast<int32_t>(layout::HeaderSize);

    // params: 0=a, 1=b; locals: 2=pa, 3=pb, 4=la, 5=lb, 6=p
    auto* body = e.block({
        e.set(2, e.wrap(e.get64(0))),
        e.set(3, e.wrap(e.get64(1))),
        e.set(4, e.load32(e.get32(2), layout::LengthOffset)),
        e.set(5, e.load32(e.get32(3), layout::LengthOffset)),
        e.set(6, e.call("endo_str_alloc", { e.bin(BinaryenAddInt32(), e.get32(4), e.get32(5)) }, I32())),
        BinaryenMemoryCopy(_module,
                           e.bin(BinaryenAddInt32(), e.get32(6), e.i32(headerSize)),
                           e.bin(BinaryenAddInt32(), e.get32(2), e.i32(headerSize)),
                           e.get32(4),
                           "0",
                           "0"),
        BinaryenMemoryCopy(
            _module,
            e.bin(BinaryenAddInt32(), e.bin(BinaryenAddInt32(), e.get32(6), e.i32(headerSize)), e.get32(4)),
            e.bin(BinaryenAddInt32(), e.get32(3), e.i32(headerSize)),
            e.get32(5),
            "0",
            "0"),
        e.ret(e.extendU(e.get32(6))),
    });

    e.addFunction("endo_str_concat", { I64(), I64() }, I64(), { I32(), I32(), I32(), I32(), I32() }, body);
}

// endo_str_len(s: i64) -> i64
void WasmRuntime::buildStrLen()
{
    auto e = Emit { _module };
    auto* body = e.ret(e.extendU(e.load32(e.wrap(e.get64(0)), layout::LengthOffset)));
    e.addFunction("endo_str_len", { I64() }, I64(), {}, body);
}

// endo_str_eq(a: i64, b: i64) -> i64 — content equality (0/1).
void WasmRuntime::buildStrEq()
{
    require("endo_mem_eq");
    auto e = Emit { _module };
    auto const headerSize = static_cast<int32_t>(layout::HeaderSize);

    // params: 0=a, 1=b; locals: 2=pa, 3=pb, 4=la
    auto* body = e.block({
        e.set(2, e.wrap(e.get64(0))),
        e.set(3, e.wrap(e.get64(1))),
        e.ifThen(e.bin(BinaryenEqInt32(), e.get32(2), e.get32(3)), e.ret(e.i64(1))),
        e.set(4, e.load32(e.get32(2), layout::LengthOffset)),
        e.ifThen(e.bin(BinaryenNeInt32(), e.get32(4), e.load32(e.get32(3), layout::LengthOffset)),
                 e.ret(e.i64(0))),
        e.ret(e.extendU(e.call("endo_mem_eq",
                               { e.bin(BinaryenAddInt32(), e.get32(2), e.i32(headerSize)),
                                 e.bin(BinaryenAddInt32(), e.get32(3), e.i32(headerSize)),
                                 e.get32(4) },
                               I32()))),
    });

    e.addFunction("endo_str_eq", { I64(), I64() }, I64(), { I32(), I32(), I32() }, body);
}

// endo_str_cmp(a: i64, b: i64) -> i64 — three-way lexicographic byte order
// with length tie-break (std::string ordering).
void WasmRuntime::buildStrCmp()
{
    auto e = Emit { _module };
    auto const headerSize = static_cast<int32_t>(layout::HeaderSize);

    // params: 0=a, 1=b; locals: 2=pa, 3=pb, 4=la, 5=lb, 6=n, 7=i, 8=ca, 9=cb
    auto* body = e.block({
        e.set(2, e.wrap(e.get64(0))),
        e.set(3, e.wrap(e.get64(1))),
        e.set(4, e.load32(e.get32(2), layout::LengthOffset)),
        e.set(5, e.load32(e.get32(3), layout::LengthOffset)),
        e.set(6,
              BinaryenSelect(
                  _module, e.bin(BinaryenLtUInt32(), e.get32(4), e.get32(5)), e.get32(4), e.get32(5))),
        e.block({ e.loop("cmp",
                         e.block({
                             e.brIf("cmp.done", e.bin(BinaryenGeUInt32(), e.get32(7), e.get32(6))),
                             e.set(8,
                                   e.load8u(e.bin(BinaryenAddInt32(),
                                                  e.bin(BinaryenAddInt32(), e.get32(2), e.i32(headerSize)),
                                                  e.get32(7)))),
                             e.set(9,
                                   e.load8u(e.bin(BinaryenAddInt32(),
                                                  e.bin(BinaryenAddInt32(), e.get32(3), e.i32(headerSize)),
                                                  e.get32(7)))),
                             e.ifThen(e.bin(BinaryenLtUInt32(), e.get32(8), e.get32(9)), e.ret(e.i64(-1))),
                             e.ifThen(e.bin(BinaryenGtUInt32(), e.get32(8), e.get32(9)), e.ret(e.i64(1))),
                             e.set(7, e.bin(BinaryenAddInt32(), e.get32(7), e.i32(1))),
                             e.br("cmp"),
                         })) },
                "cmp.done"),
        e.ifThen(e.bin(BinaryenLtUInt32(), e.get32(4), e.get32(5)), e.ret(e.i64(-1))),
        e.ifThen(e.bin(BinaryenGtUInt32(), e.get32(4), e.get32(5)), e.ret(e.i64(1))),
        e.ret(e.i64(0)),
    });

    e.addFunction("endo_str_cmp",
                  { I64(), I64() },
                  I64(),
                  { I32(), I32(), I32(), I32(), I32(), I32(), I32(), I32() },
                  body);
}

// endo_str_starts_with(s: i64, prefix: i64) -> i64
void WasmRuntime::buildStrStartsWith()
{
    require("endo_mem_eq");
    auto e = Emit { _module };
    auto const headerSize = static_cast<int32_t>(layout::HeaderSize);

    // params: 0=s, 1=prefix; locals: 2=ps, 3=pp, 4=lp
    auto* body = e.block({
        e.set(2, e.wrap(e.get64(0))),
        e.set(3, e.wrap(e.get64(1))),
        e.set(4, e.load32(e.get32(3), layout::LengthOffset)),
        e.ifThen(e.bin(BinaryenGtUInt32(), e.get32(4), e.load32(e.get32(2), layout::LengthOffset)),
                 e.ret(e.i64(0))),
        e.ret(e.extendU(e.call("endo_mem_eq",
                               { e.bin(BinaryenAddInt32(), e.get32(2), e.i32(headerSize)),
                                 e.bin(BinaryenAddInt32(), e.get32(3), e.i32(headerSize)),
                                 e.get32(4) },
                               I32()))),
    });

    e.addFunction("endo_str_starts_with", { I64(), I64() }, I64(), { I32(), I32(), I32() }, body);
}

// endo_str_ends_with(s: i64, suffix: i64) -> i64
void WasmRuntime::buildStrEndsWith()
{
    require("endo_mem_eq");
    auto e = Emit { _module };
    auto const headerSize = static_cast<int32_t>(layout::HeaderSize);

    // params: 0=s, 1=suffix; locals: 2=ps, 3=pq, 4=lq, 5=ls
    auto* body = e.block({
        e.set(2, e.wrap(e.get64(0))),
        e.set(3, e.wrap(e.get64(1))),
        e.set(4, e.load32(e.get32(3), layout::LengthOffset)),
        e.set(5, e.load32(e.get32(2), layout::LengthOffset)),
        e.ifThen(e.bin(BinaryenGtUInt32(), e.get32(4), e.get32(5)), e.ret(e.i64(0))),
        e.ret(e.extendU(e.call("endo_mem_eq",
                               { e.bin(BinaryenSubInt32(),
                                       e.bin(BinaryenAddInt32(),
                                             e.bin(BinaryenAddInt32(), e.get32(2), e.i32(headerSize)),
                                             e.get32(5)),
                                       e.get32(4)),
                                 e.bin(BinaryenAddInt32(), e.get32(3), e.i32(headerSize)),
                                 e.get32(4) },
                               I32()))),
    });

    e.addFunction("endo_str_ends_with", { I64(), I64() }, I64(), { I32(), I32(), I32(), I32() }, body);
}

// endo_str_contains(s: i64, needle: i64) -> i64 — naive substring search.
void WasmRuntime::buildStrContains()
{
    require("endo_mem_eq");
    auto e = Emit { _module };
    auto const headerSize = static_cast<int32_t>(layout::HeaderSize);

    // params: 0=s, 1=needle; locals: 2=ps, 3=pn, 4=ls, 5=ln, 6=start
    auto* body = e.block({
        e.set(2, e.wrap(e.get64(0))),
        e.set(3, e.wrap(e.get64(1))),
        e.set(4, e.load32(e.get32(2), layout::LengthOffset)),
        e.set(5, e.load32(e.get32(3), layout::LengthOffset)),
        e.ifThen(e.un(BinaryenEqZInt32(), e.get32(5)), e.ret(e.i64(1))),
        e.ifThen(e.bin(BinaryenGtUInt32(), e.get32(5), e.get32(4)), e.ret(e.i64(0))),
        e.block({ e.loop("scan",
                         e.block({
                             e.brIf("scan.done",
                                    e.bin(BinaryenGtUInt32(),
                                          e.bin(BinaryenAddInt32(), e.get32(6), e.get32(5)),
                                          e.get32(4))),
                             e.ifThen(e.call("endo_mem_eq",
                                             { e.bin(BinaryenAddInt32(),
                                                     e.bin(BinaryenAddInt32(), e.get32(2), e.i32(headerSize)),
                                                     e.get32(6)),
                                               e.bin(BinaryenAddInt32(), e.get32(3), e.i32(headerSize)),
                                               e.get32(5) },
                                             I32()),
                                      e.ret(e.i64(1))),
                             e.set(6, e.bin(BinaryenAddInt32(), e.get32(6), e.i32(1))),
                             e.br("scan"),
                         })) },
                "scan.done"),
        e.ret(e.i64(0)),
    });

    e.addFunction("endo_str_contains", { I64(), I64() }, I64(), { I32(), I32(), I32(), I32(), I32() }, body);
}

// endo_str_to_i64(s: i64) -> i64 — permissive decimal parse: optional
// leading whitespace and sign, digits until the first non-digit, else 0.
void WasmRuntime::buildStrToI64()
{
    auto e = Emit { _module };
    auto const headerSize = static_cast<int32_t>(layout::HeaderSize);

    // params: 0=s; locals: 1=p, 2=len, 3=i, 4=c, 5=neg, 6=acc(i64)
    auto const currentChar = [&]() {
        return e.load8u(
            e.bin(BinaryenAddInt32(), e.bin(BinaryenAddInt32(), e.get32(1), e.i32(headerSize)), e.get32(3)));
    };
    auto const atEnd = [&]() {
        return e.bin(BinaryenGeUInt32(), e.get32(3), e.get32(2));
    };

    auto* body = e.block({
        e.set(1, e.wrap(e.get64(0))),
        e.set(2, e.load32(e.get32(1), layout::LengthOffset)),
        // skip leading spaces and tabs
        e.block({ e.loop("ws",
                         e.block({
                             e.brIf("ws.done", atEnd()),
                             e.set(4, currentChar()),
                             e.brIf("ws.done",
                                    e.bin(BinaryenAndInt32(),
                                          e.bin(BinaryenNeInt32(), e.get32(4), e.i32(' ')),
                                          e.bin(BinaryenNeInt32(), e.get32(4), e.i32('\t')))),
                             e.set(3, e.bin(BinaryenAddInt32(), e.get32(3), e.i32(1))),
                             e.br("ws"),
                         })) },
                "ws.done"),
        // optional sign
        e.ifThen(e.un(BinaryenEqZInt32(), atEnd()),
                 e.block({
                     e.set(4, currentChar()),
                     e.ifThen(e.bin(BinaryenEqInt32(), e.get32(4), e.i32('-')),
                              e.block({
                                  e.set(5, e.i32(1)),
                                  e.set(3, e.bin(BinaryenAddInt32(), e.get32(3), e.i32(1))),
                              })),
                     e.ifThen(e.bin(BinaryenEqInt32(), e.get32(4), e.i32('+')),
                              e.set(3, e.bin(BinaryenAddInt32(), e.get32(3), e.i32(1)))),
                 })),
        // digits
        e.block({ e.loop("digits",
                         e.block({
                             e.brIf("digits.done", atEnd()),
                             e.set(4, currentChar()),
                             e.brIf("digits.done", e.bin(BinaryenLtUInt32(), e.get32(4), e.i32('0'))),
                             e.brIf("digits.done", e.bin(BinaryenGtUInt32(), e.get32(4), e.i32('9'))),
                             e.set(6,
                                   e.bin(BinaryenAddInt64(),
                                         e.bin(BinaryenMulInt64(), e.get64(6), e.i64(10)),
                                         e.extendU(e.bin(BinaryenSubInt32(), e.get32(4), e.i32('0'))))),
                             e.set(3, e.bin(BinaryenAddInt32(), e.get32(3), e.i32(1))),
                             e.br("digits"),
                         })) },
                "digits.done"),
        e.ifThen(e.get32(5), e.set(6, e.bin(BinaryenSubInt64(), e.i64(0), e.get64(6)))),
        e.ret(e.get64(6)),
    });

    e.addFunction("endo_str_to_i64", { I64() }, I64(), { I32(), I32(), I32(), I32(), I32(), I64() }, body);
}

// endo_str_slice(s: i64, start: i64, len: i64) -> i64 — copies a byte range
// into a fresh string (bounds are the caller's responsibility).
void WasmRuntime::buildStrSlice()
{
    require("endo_str_alloc");
    auto e = Emit { _module };
    auto const headerSize = static_cast<int32_t>(layout::HeaderSize);

    // params: 0=s, 1=start, 2=len; locals: 3=p
    auto* body = e.block({
        e.set(3, e.call("endo_str_alloc", { e.wrap(e.get64(2)) }, I32())),
        BinaryenMemoryCopy(_module,
                           e.bin(BinaryenAddInt32(), e.get32(3), e.i32(headerSize)),
                           e.bin(BinaryenAddInt32(),
                                 e.bin(BinaryenAddInt32(), e.wrap(e.get64(0)), e.i32(headerSize)),
                                 e.wrap(e.get64(1))),
                           e.wrap(e.get64(2)),
                           "0",
                           "0"),
        e.ret(e.extendU(e.get32(3))),
    });

    e.addFunction("endo_str_slice", { I64(), I64(), I64() }, I64(), { I32() }, body);
}

// endo_obj_alloc(typeId: i64, slotCount: i64) -> i64 — allocates an object
// cell; slots are zero (fresh memory, never reused).
void WasmRuntime::buildObjAlloc()
{
    require("endo_alloc");
    auto e = Emit { _module };

    // params: 0=typeId, 1=slotCount; locals: 2=p
    auto* body = e.block({
        e.set(2,
              e.call("endo_alloc",
                     { e.bin(BinaryenAddInt32(),
                             e.i32(static_cast<int32_t>(layout::HeaderSize)),
                             e.bin(BinaryenShlInt32(), e.wrap(e.get64(1)), e.i32(3))) },
                     I32())),
        e.store8(e.get32(2), e.i32(layout::KindObject)),
        e.store16(e.get32(2), e.wrap(e.get64(0)), layout::TypeIdOffset),
        e.ret(e.extendU(e.get32(2))),
    });

    e.addFunction("endo_obj_alloc", { I64(), I64() }, I64(), { I32() }, body);
}

// endo_f64_to_str_fixed(f: f64) -> i64 — %f-style formatting with 6
// fractional digits and trailing-zero trimming (at least one digit kept),
// replicating slotValueToString's Float branch. Magnitudes beyond the i64
// range degrade to a saturated integer part (documented deviation).
void WasmRuntime::buildF64ToStrFixed()
{
    require("endo_i64_to_str");
    require("endo_str_concat");
    require("endo_str_slice");
    auto e = Emit { _module };
    auto const nan = static_cast<int64_t>(_strings->intern("nan"));
    auto const inf = static_cast<int64_t>(_strings->intern("inf"));
    auto const negInf = static_cast<int64_t>(_strings->intern("-inf"));
    auto const minus = static_cast<int64_t>(_strings->intern("-"));
    auto const dot = static_cast<int64_t>(_strings->intern("."));
    auto const infinity = std::numeric_limits<double>::infinity();

    // params: 0=f; locals: 1=abs f64, 2=ip i64, 3=micro i64, 4=intStr i64,
    //                      5=fracStr i64, 6=keep i32, 7=fracPtr i32
    auto* body = e.block({
        e.ifThen(e.bin(BinaryenNeFloat64(), e.getF64(0), e.getF64(0)), e.ret(e.i64(nan))),
        e.ifThen(e.bin(BinaryenEqFloat64(), e.getF64(0), e.f64(infinity)), e.ret(e.i64(inf))),
        e.ifThen(e.bin(BinaryenEqFloat64(), e.getF64(0), e.f64(-infinity)), e.ret(e.i64(negInf))),
        e.set(1, e.un(BinaryenAbsFloat64(), e.getF64(0))),
        // integer part and rounded 6-digit fraction
        e.set(2, e.un(BinaryenTruncSatSFloat64ToInt64(), e.getF64(1))),
        e.set(3,
              e.un(BinaryenTruncSatSFloat64ToInt64(),
                   e.bin(BinaryenAddFloat64(),
                         e.bin(BinaryenMulFloat64(),
                               e.bin(BinaryenSubFloat64(),
                                     e.getF64(1),
                                     e.un(BinaryenConvertSInt64ToFloat64(), e.get64(2))),
                               e.f64(1'000'000.0)),
                         e.f64(0.5)))),
        e.ifThen(e.bin(BinaryenGeSInt64(), e.get64(3), e.i64(1'000'000)),
                 e.block({
                     e.set(2, e.bin(BinaryenAddInt64(), e.get64(2), e.i64(1))),
                     e.set(3, e.i64(0)),
                 })),
        e.set(4, e.call("endo_i64_to_str", { e.get64(2) }, I64())),
        e.ifThen(e.bin(BinaryenLtFloat64(), e.getF64(0), e.f64(0.0)),
                 e.set(4, e.call("endo_str_concat", { e.i64(minus), e.get64(4) }, I64()))),
        // "1DDDDDD" gives six zero-padded fraction digits after the first char
        e.set(5,
              e.call("endo_i64_to_str", { e.bin(BinaryenAddInt64(), e.get64(3), e.i64(1'000'000)) }, I64())),
        e.set(7, e.wrap(e.get64(5))),
        e.set(6, e.i32(6)),
        e.block(
            { e.loop("trim",
                     e.block({
                         e.brIf("trim.done", e.bin(BinaryenLeUInt32(), e.get32(6), e.i32(1))),
                         e.brIf("trim.done",
                                e.bin(BinaryenNeInt32(),
                                      e.load8u(e.bin(BinaryenAddInt32(),
                                                     e.bin(BinaryenAddInt32(),
                                                           e.get32(7),
                                                           e.i32(static_cast<int32_t>(layout::HeaderSize))),
                                                     e.get32(6))),
                                      e.i32('0'))),
                         e.set(6, e.bin(BinaryenSubInt32(), e.get32(6), e.i32(1))),
                         e.br("trim"),
                     })) },
            "trim.done"),
        e.set(4, e.call("endo_str_concat", { e.get64(4), e.i64(dot) }, I64())),
        e.ret(e.call(
            "endo_str_concat",
            { e.get64(4), e.call("endo_str_slice", { e.get64(5), e.i64(1), e.extendU(e.get32(6)) }, I64()) },
            I64())),
    });

    e.addFunction("endo_f64_to_str_fixed",
                  { BinaryenTypeFloat64() },
                  I64(),
                  { BinaryenTypeFloat64(), I64(), I64(), I64(), I64(), I32(), I32() },
                  body);
}

namespace
{
    // LiteralType values baked into formatter dispatch (see CoreVM/enums.hpp).
    constexpr int32_t LiteralBoolean = 1;
    constexpr int32_t LiteralNumber = 2;
    constexpr int32_t LiteralString = 3;
    constexpr int32_t LiteralFloat = 17;

    // BuiltinTypeId values (see CoreVM/types/TypeDescriptor.hpp).
    constexpr int32_t TypeIdOption = 1;
    constexpr int32_t TypeIdResult = 2;
    constexpr int32_t TypeIdTuple2 = 3;
    constexpr int32_t TypeIdTuple3 = 4;
    constexpr int32_t TypeIdList = 5;
} // namespace

// endo_slot_to_str(v: i64, litType: i64, quote: i64) -> i64 — renders one
// container slot according to its element LiteralType, mirroring
// slotValueToString (strings quoted inside containers).
void WasmRuntime::buildSlotToStr()
{
    require("endo_i64_to_str");
    require("endo_str_concat");
    require("endo_f64_to_str_fixed");
    require("endo_value_to_str");
    auto e = Emit { _module };
    auto const nullStr = static_cast<int64_t>(_strings->intern("(null)"));
    auto const quoteStr = static_cast<int64_t>(_strings->intern("\""));
    auto const trueStr = static_cast<int64_t>(_strings->intern("true"));
    auto const falseStr = static_cast<int64_t>(_strings->intern("false"));

    // params: 0=v, 1=litType, 2=quote; locals: 3=lt i32
    auto* body = e.block({
        e.set(3, e.wrap(e.get64(1))),
        e.ifThen(e.bin(BinaryenEqInt32(), e.get32(3), e.i32(LiteralString)),
                 e.block({
                     e.ifThen(e.un(BinaryenEqZInt32(), e.wrap(e.get64(0))), e.ret(e.i64(nullStr))),
                     e.ifThen(e.un(BinaryenEqZInt64(), e.get64(2)), e.ret(e.get64(0))),
                     e.ret(e.call("endo_str_concat",
                                  { e.call("endo_str_concat", { e.i64(quoteStr), e.get64(0) }, I64()),
                                    e.i64(quoteStr) },
                                  I64())),
                 })),
        e.ifThen(e.bin(BinaryenEqInt32(), e.get32(3), e.i32(LiteralBoolean)),
                 e.ret(BinaryenSelect(
                     _module, e.un(BinaryenEqZInt64(), e.get64(0)), e.i64(falseStr), e.i64(trueStr)))),
        e.ifThen(e.bin(BinaryenEqInt32(), e.get32(3), e.i32(LiteralNumber)),
                 e.ret(e.call("endo_i64_to_str", { e.get64(0) }, I64()))),
        e.ifThen(
            e.bin(BinaryenEqInt32(), e.get32(3), e.i32(LiteralFloat)),
            e.ret(e.call("endo_f64_to_str_fixed", { e.un(BinaryenReinterpretInt64(), e.get64(0)) }, I64()))),
        e.ret(e.call("endo_value_to_str", { e.get64(0) }, I64())),
    });

    e.addFunction("endo_slot_to_str", { I64(), I64(), I64() }, I64(), { I32() }, body);
}

// endo_value_to_str(v: i64) -> i64 — the recursive dynamic-value classifier
// behind object_to_string: strings pass through, known composite objects
// dispatch to their formatter, everything else formats as a number.
void WasmRuntime::buildValueToStr()
{
    require("endo_i64_to_str");
    require("endo_list_to_string");
    require("endo_option_str");
    require("endo_result_str");
    require("endo_tuple2_str");
    require("endo_tuple3_str");
    auto e = Emit { _module };
    auto const placeholder = static_cast<int64_t>(_strings->intern("<object>"));

    // params: 0=v; locals: 1=p, 2=typeId
    auto* const plausiblePointer =
        e.bin(BinaryenAndInt32(),
              e.bin(BinaryenGeUInt32(), e.get32(1), e.i32(static_cast<int32_t>(layout::DataBase))),
              e.bin(BinaryenAndInt32(),
                    e.un(BinaryenEqZInt32(), e.bin(BinaryenAndInt32(), e.get32(1), e.i32(7))),
                    e.bin(BinaryenLtUInt32(), e.get32(1), e.heapPtr())));

    auto const dispatch = [&](int32_t typeId, char const* formatter) {
        return e.ifThen(e.bin(BinaryenEqInt32(), e.get32(2), e.i32(typeId)),
                        e.ret(e.call(formatter, { e.get64(0) }, I64())));
    };

    auto* body = e.block({
        e.set(1, e.wrap(e.get64(0))),
        e.ifThen(plausiblePointer,
                 e.block({
                     e.ifThen(e.bin(BinaryenEqInt32(), e.load8u(e.get32(1)), e.i32(layout::KindString)),
                              e.ret(e.get64(0))),
                     e.ifThen(e.bin(BinaryenEqInt32(), e.load8u(e.get32(1)), e.i32(layout::KindObject)),
                              e.block({
                                  e.set(2, e.load16u(e.get32(1), layout::TypeIdOffset)),
                                  dispatch(TypeIdList, "endo_list_to_string"),
                                  dispatch(TypeIdOption, "endo_option_str"),
                                  dispatch(TypeIdResult, "endo_result_str"),
                                  dispatch(TypeIdTuple2, "endo_tuple2_str"),
                                  dispatch(TypeIdTuple3, "endo_tuple3_str"),
                                  e.ret(e.i64(placeholder)),
                              })),
                 })),
        e.ret(e.call("endo_i64_to_str", { e.get64(0) }, I64())),
    });

    e.addFunction("endo_value_to_str", { I64() }, I64(), { I32(), I32() }, body);
}

// endo_list_to_string(v: i64) -> i64 — "[e; e; e]" with the element type
// from cons-cell slot 2 (see formatList in TypeFormatters.cpp).
void WasmRuntime::buildListToString()
{
    require("endo_str_concat");
    require("endo_slot_to_str");
    auto e = Emit { _module };
    auto const headerSize = layout::HeaderSize;
    auto const lbracket = static_cast<int64_t>(_strings->intern("["));
    auto const rbracket = static_cast<int64_t>(_strings->intern("]"));
    auto const separator = static_cast<int64_t>(_strings->intern("; "));

    // params: 0=v; locals: 1=cur i32, 2=result i64, 3=elemType i64, 4=first i32
    auto* body = e.block({
        e.set(1, e.wrap(e.get64(0))),
        e.set(2, e.i64(lbracket)),
        e.set(4, e.i32(1)),
        e.ifThen(e.get32(1), e.set(3, e.load64(e.get32(1), headerSize + (2 * layout::SlotSize)))),
        e.block(
            { e.loop(
                "cells",
                e.block({
                    // continue only while cur is a Cons cell of a List
                    e.brIf("cells.done", e.un(BinaryenEqZInt32(), e.get32(1))),
                    e.brIf("cells.done",
                           e.bin(BinaryenNeInt32(), e.load8u(e.get32(1)), e.i32(layout::KindObject))),
                    e.brIf("cells.done",
                           e.bin(BinaryenNeInt32(),
                                 e.load16u(e.get32(1), layout::TypeIdOffset),
                                 e.i32(TypeIdList))),
                    e.brIf("cells.done",
                           e.bin(BinaryenNeInt32(), e.load8u(e.get32(1), layout::TagOffset), e.i32(1))),
                    e.ifThen(e.un(BinaryenEqZInt32(), e.get32(4)),
                             e.set(2, e.call("endo_str_concat", { e.get64(2), e.i64(separator) }, I64()))),
                    e.set(4, e.i32(0)),
                    e.set(2,
                          e.call("endo_str_concat",
                                 { e.get64(2),
                                   e.call("endo_slot_to_str",
                                          { e.load64(e.get32(1), headerSize), e.get64(3), e.i64(1) },
                                          I64()) },
                                 I64())),
                    e.set(1, e.wrap(e.load64(e.get32(1), headerSize + layout::SlotSize))),
                    e.br("cells"),
                })) },
            "cells.done"),
        e.ret(e.call("endo_str_concat", { e.get64(2), e.i64(rbracket) }, I64())),
    });

    e.addFunction("endo_list_to_string", { I64() }, I64(), { I32(), I64(), I64(), I32() }, body);
}

// endo_option_str(v: i64) -> i64 — "None" or "Some <slot0>" (inner type in slot 1).
void WasmRuntime::buildOptionStr()
{
    require("endo_str_concat");
    require("endo_slot_to_str");
    auto e = Emit { _module };
    auto const none = static_cast<int64_t>(_strings->intern("None"));
    auto const some = static_cast<int64_t>(_strings->intern("Some "));

    // params: 0=v; locals: 1=p
    auto* body = e.block({
        e.set(1, e.wrap(e.get64(0))),
        e.ifThen(e.un(BinaryenEqZInt32(), e.load8u(e.get32(1), layout::TagOffset)), e.ret(e.i64(none))),
        e.ret(e.call("endo_str_concat",
                     { e.i64(some),
                       e.call("endo_slot_to_str",
                              { e.load64(e.get32(1), layout::HeaderSize),
                                e.load64(e.get32(1), layout::HeaderSize + layout::SlotSize),
                                e.i64(1) },
                              I64()) },
                     I64())),
    });

    e.addFunction("endo_option_str", { I64() }, I64(), { I32() }, body);
}

// endo_result_str(v: i64) -> i64 — "Ok <slot0>" / "Error <slot0>".
void WasmRuntime::buildResultStr()
{
    require("endo_str_concat");
    require("endo_slot_to_str");
    auto e = Emit { _module };
    auto const okPrefix = static_cast<int64_t>(_strings->intern("Ok "));
    auto const errorPrefix = static_cast<int64_t>(_strings->intern("Error "));

    // params: 0=v; locals: 1=p
    auto* body = e.block({
        e.set(1, e.wrap(e.get64(0))),
        e.ret(e.call(
            "endo_str_concat",
            { BinaryenSelect(
                  _module, e.load8u(e.get32(1), layout::TagOffset), e.i64(okPrefix), e.i64(errorPrefix)),
              e.call("endo_slot_to_str",
                     { e.load64(e.get32(1), layout::HeaderSize),
                       e.load64(e.get32(1), layout::HeaderSize + layout::SlotSize),
                       e.i64(1) },
                     I64()) },
            I64())),
    });

    e.addFunction("endo_result_str", { I64() }, I64(), { I32() }, body);
}

// endo_tuple2_str(v: i64) -> i64 — "(a, b)"; element types are packed one
// byte each into slot 2.
void WasmRuntime::buildTuple2Str()
{
    require("endo_str_concat");
    require("endo_slot_to_str");
    auto e = Emit { _module };
    auto const lparen = static_cast<int64_t>(_strings->intern("("));
    auto const rparen = static_cast<int64_t>(_strings->intern(")"));
    auto const comma = static_cast<int64_t>(_strings->intern(", "));

    auto const slotValue = [&](uint32_t index) {
        return e.load64(e.get32(1), layout::HeaderSize + (index * layout::SlotSize));
    };
    auto const packedType = [&](uint32_t index) {
        return e.bin(BinaryenAndInt64(),
                     e.bin(BinaryenShrUInt64(),
                           e.load64(e.get32(1), layout::HeaderSize + (2 * layout::SlotSize)),
                           e.i64(8L * index)),
                     e.i64(0xFF));
    };
    auto const concat = [&](BinaryenExpressionRef a, BinaryenExpressionRef b) {
        return e.call("endo_str_concat", { a, b }, I64());
    };
    auto const element = [&](uint32_t index) {
        return e.call("endo_slot_to_str", { slotValue(index), packedType(index), e.i64(1) }, I64());
    };

    // params: 0=v; locals: 1=p, 2=result i64
    auto* body = e.block({
        e.set(1, e.wrap(e.get64(0))),
        e.set(2, concat(e.i64(lparen), element(0))),
        e.set(2, concat(e.get64(2), e.i64(comma))),
        e.set(2, concat(e.get64(2), element(1))),
        e.ret(concat(e.get64(2), e.i64(rparen))),
    });

    e.addFunction("endo_tuple2_str", { I64() }, I64(), { I32(), I64() }, body);
}

// endo_tuple3_str(v: i64) -> i64 — "(a, b, c)"; packed types in slot 3.
void WasmRuntime::buildTuple3Str()
{
    require("endo_str_concat");
    require("endo_slot_to_str");
    auto e = Emit { _module };
    auto const lparen = static_cast<int64_t>(_strings->intern("("));
    auto const rparen = static_cast<int64_t>(_strings->intern(")"));
    auto const comma = static_cast<int64_t>(_strings->intern(", "));

    auto const slotValue = [&](uint32_t index) {
        return e.load64(e.get32(1), layout::HeaderSize + (index * layout::SlotSize));
    };
    auto const packedType = [&](uint32_t index) {
        return e.bin(BinaryenAndInt64(),
                     e.bin(BinaryenShrUInt64(),
                           e.load64(e.get32(1), layout::HeaderSize + (3 * layout::SlotSize)),
                           e.i64(8L * index)),
                     e.i64(0xFF));
    };
    auto const concat = [&](BinaryenExpressionRef a, BinaryenExpressionRef b) {
        return e.call("endo_str_concat", { a, b }, I64());
    };
    auto const element = [&](uint32_t index) {
        return e.call("endo_slot_to_str", { slotValue(index), packedType(index), e.i64(1) }, I64());
    };

    // params: 0=v; locals: 1=p, 2=result i64
    auto* body = e.block({
        e.set(1, e.wrap(e.get64(0))),
        e.set(2, concat(e.i64(lparen), element(0))),
        e.set(2, concat(e.get64(2), e.i64(comma))),
        e.set(2, concat(e.get64(2), element(1))),
        e.set(2, concat(e.get64(2), e.i64(comma))),
        e.set(2, concat(e.get64(2), element(2))),
        e.ret(concat(e.get64(2), e.i64(rparen))),
    });

    e.addFunction("endo_tuple3_str", { I64() }, I64(), { I32(), I64() }, body);
}

} // namespace CoreVM::wasm
