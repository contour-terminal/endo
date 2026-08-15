// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <endo-language/CompileToIR.hpp>
#include <endo-language/builtins/StubRuntime.hpp>

#include <CoreVM/wasm/WasmCodeGenerator.hpp>
#include <CoreVM/wasm/WasmRuntime.hpp>

#include <optional>
#include <string>
#include <string_view>

// NOTE: This header requires the WASM backend (ENDO_HAS_WASM); include it
// only from translation units gated on that feature.

namespace endo
{

/// Compiles endo source text to a WebAssembly module.
///
/// This is the single source-to-wasm pipeline shared by `endo -o`
/// (CompileCommand) and endo-test's `# mode: wasm`, so the tests always
/// exercise exactly what the CLI ships.
///
/// The CoreVM IR-level optimization passes (PassManager) are deliberately
/// not run here: they verify() the IR after each change, and the frontend
/// routinely emits blocks that fail verification (e.g. unterminated match
/// merge blocks after fully-returning arms) yet execute fine. Optimization
/// happens at the WASM module level via binaryen (WasmOptions::optimize).
///
/// @param source     the script source (shebang already stripped)
/// @param sourceName name used in diagnostics (e.g. the file path)
/// @param options    WASM backend options
/// @param report     receives parse, IR and WASM-lowering diagnostics
/// @return the compiled module, or std::nullopt on any failure (see @p report)
[[nodiscard]] inline std::optional<CoreVM::wasm::WasmOutput> compileSourceToWasm(
    std::string source,
    std::string_view sourceName,
    CoreVM::wasm::WasmOptions const& options,
    CoreVM::diagnostics::Report& report)
{
    // IR generation only needs builtin signatures, not executable callbacks.
    auto runtime = CoreVM::Runtime {};
    registerStubRuntime(runtime);

    auto irProgram = compileToIR(std::move(source), runtime, report, sourceName);
    if (!irProgram)
        return std::nullopt;

    auto provider = CoreVM::wasm::WasmRuntime {};
    return CoreVM::wasm::WasmCodeGenerator(provider, options).generate(irProgram.get(), report);
}

} // namespace endo
