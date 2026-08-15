// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <CoreVM/Diagnostics.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace CoreVM
{
class IRProgram;
}

namespace CoreVM::wasm
{

class WasmRuntimeProvider;

/// Result of a successful WASM compilation.
struct WasmOutput
{
    std::vector<uint8_t> binary; ///< The .wasm binary module.
    std::string wat;             ///< The module in text format (only when WasmOptions::emitWat).
};

/// Compilation options for the WASM backend.
struct WasmOptions
{
    bool optimize = false; ///< Run binaryen's optimizer over the generated module.
    bool tailCalls = true; ///< Emit return_call for tail calls (requires the tail-call proposal).
    bool emitWat = false;  ///< Additionally produce the text format in WasmOutput::wat.
};

/// Compiles a CoreVM IRProgram to a WebAssembly module (WASI command).
///
/// This is the WASM analog of TargetCodeGenerator: it consumes the same
/// SSA-form IR and produces a self-contained `.wasm` binary whose `_start`
/// export runs the program under any WASI runtime (e.g. wasmtime).
///
/// The WASM-side runtime (allocator, string helpers, WASI shims) is supplied
/// via the injected WasmRuntimeProvider.
class WasmCodeGenerator
{
  public:
    /// @param runtimeProvider supplies the in-module runtime helper functions
    /// @param options         compilation options
    explicit WasmCodeGenerator(WasmRuntimeProvider& runtimeProvider, WasmOptions options = {});

    /// Generates a WASM module from the given IR program.
    ///
    /// Unsupported IR constructs are collected as diagnostics in @p report
    /// (all of them, not just the first) and result in std::nullopt.
    ///
    /// @param program the IR program to compile (mutated: cross-block values are materialized)
    /// @param report  receives diagnostics for unsupported constructs and internal errors
    /// @return the compiled module, or std::nullopt if any diagnostic was reported
    [[nodiscard]] std::optional<WasmOutput> generate(IRProgram* program, diagnostics::Report& report);

  private:
    WasmRuntimeProvider& _runtimeProvider;
    WasmOptions _options;
};

} // namespace CoreVM::wasm
