// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string_view>

/// @file
/// Data-driven mapping from native builtin signatures (as they appear on
/// CallInstr, e.g. "println(S)V") to their WASM lowering. A builtin that is
/// not in this table cannot be compiled to WebAssembly and produces a clean
/// compile-time diagnostic.

namespace CoreVM::wasm
{

/// Special-cased lowerings that do not map 1:1 to a runtime helper call.
enum class BuiltinInlineOp : uint8_t
{
    None,           ///< Not inline: call the runtime helper.
    ProcExit,       ///< Call WASI proc_exit with the (wrapped) i32 argument.
    SetExitStatus,  ///< Store the argument into the exit-status global.
    Ignore,         ///< Emit nothing (e.g. GC write barriers).
    ObjectToString, ///< Value-to-string with compile-time type dispatch: statically
                    ///< Number/Boolean-typed arguments use the integer formatter,
                    ///< String-typed pass through, and only dynamically typed
                    ///< values go through the runtime classifier.
};

/// One row of the builtin mapping table.
struct WasmBuiltinDescriptor
{
    std::string_view signature;     ///< Builtin signature, e.g. "println(S)V".
    std::string_view runtimeHelper; ///< Runtime helper implementing it (when inlineOp is None).
    BuiltinInlineOp inlineOp = BuiltinInlineOp::None;
};

/// Looks up the WASM lowering for a builtin signature.
/// @return the descriptor, or nullptr if the builtin is unsupported.
[[nodiscard]] WasmBuiltinDescriptor const* findWasmBuiltin(std::string_view signature);

} // namespace CoreVM::wasm
