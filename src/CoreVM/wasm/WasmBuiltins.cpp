// SPDX-License-Identifier: Apache-2.0
#include <CoreVM/wasm/WasmBuiltins.hpp>

#include <array>

namespace CoreVM::wasm
{

namespace
{
    /// The builtins compilable to WebAssembly. Everything else is a
    /// compile-time error. Rows are added milestone by milestone as their
    /// runtime helpers come into existence.
    constexpr auto WasmBuiltins = std::to_array<WasmBuiltinDescriptor>({
        { .signature = "exit(I)V", .inlineOp = BuiltinInlineOp::ProcExit },
        { .signature = "setvar.exitstatus(I)V", .inlineOp = BuiltinInlineOp::SetExitStatus },
        { .signature = "ref_write_barrier(I)V", .inlineOp = BuiltinInlineOp::Ignore },
        { .signature = "print(S)V", .runtimeHelper = "endo_print" },
        { .signature = "println(S)V", .runtimeHelper = "endo_println" },
        { .signature = "object_to_string(I)S", .inlineOp = BuiltinInlineOp::ObjectToString },
        { .signature = "list_to_string(I)S", .runtimeHelper = "endo_list_to_string" },
        { .signature = "display_result(I)V", .runtimeHelper = "endo_display_result" },
        { .signature = "list_length(I)I", .runtimeHelper = "endo_list_length" },
        { .signature = "list_isEmpty(I)B", .runtimeHelper = "endo_list_is_empty" },
        { .signature = "list_head(I)I", .runtimeHelper = "endo_list_head" },
        { .signature = "list_tail(I)I", .runtimeHelper = "endo_list_tail" },
        { .signature = "list_nth(II)I", .runtimeHelper = "endo_list_nth" },
        { .signature = "list_concat(II)I", .runtimeHelper = "endo_list_concat" },
    });
} // namespace

WasmBuiltinDescriptor const* findWasmBuiltin(std::string_view signature)
{
    for (auto const& builtin: WasmBuiltins)
        if (builtin.signature == signature)
            return &builtin;
    return nullptr;
}

} // namespace CoreVM::wasm
