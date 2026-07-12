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
