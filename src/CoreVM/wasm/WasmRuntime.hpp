// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <CoreVM/wasm/WasmRuntimeABI.hpp>

#include <set>
#include <string>
#include <string_view>

namespace CoreVM::wasm
{

class WasmStringTable;

/// The real WASM-side runtime: builds the $endo_* helper function bodies
/// (bump allocator, string operations, number formatting, WASI-based I/O)
/// directly into the module using the binaryen C API.
///
/// Helpers are emitted lazily: only the ones referenced by generated code,
/// plus their transitive dependencies, land in the module. ABI helpers whose
/// native implementation does not exist yet degrade to imports (the module
/// still validates; instantiation then fails with a clear missing-import
/// error naming the helper).
class WasmRuntime final: public WasmRuntimeProvider
{
  public:
    void provide(BinaryenModuleRef module,
                 std::set<RuntimeHelperDef const*> const& usedHelpers,
                 WasmStringTable& strings) override;

  private:
    /// Ensures the named function exists in the module (building it and its
    /// dependencies on first use).
    void require(std::string_view name);

    /// Ensures the WASI fd_write import is present.
    void requireFdWrite();

    /// Declares an ABI helper as an import (fallback for helpers without a
    /// native builder yet).
    void importHelper(std::string_view name);

    // Helper body builders. Each corresponds to one runtime function; see
    // WasmRuntimeABI.hpp for the value-level signatures.
    void buildAlloc();
    void buildStrAlloc();
    void buildWriteAll();
    void buildPrint();
    void buildPrintln();
    void buildPanic();
    void buildI64ToStr();
    void buildObjectToString();
    void buildI64Div();
    void buildI64Rem();
    void buildI64Pow();
    void buildMemEq();
    void buildStrConcat();
    void buildStrLen();
    void buildStrEq();
    void buildStrCmp();
    void buildStrStartsWith();
    void buildStrEndsWith();
    void buildStrContains();
    void buildStrToI64();
    void buildStrSlice();
    void buildObjAlloc();
    void buildF64ToStrFixed();
    void buildSlotToStr();
    void buildValueToStr();
    void buildListToString();
    void buildOptionStr();
    void buildResultStr();
    void buildTuple2Str();
    void buildTuple3Str();

    BinaryenModuleRef _module = nullptr;
    WasmStringTable* _strings = nullptr;
    std::set<std::string, std::less<>> _built;
};

} // namespace CoreVM::wasm
