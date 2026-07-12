// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string_view>

namespace endo::compile
{

/// Options for the `endo -o <output> <script>` compile mode.
struct CompileOptions
{
    std::string_view scriptFile; ///< The .endo script to compile.
    std::string_view outputFile; ///< Output path; format is derived from the extension (.wasm, .wat).
    bool optimize = false;       ///< -O: run binaryen optimizations over the module.
    bool tailCalls = true;       ///< Emit return_call (disable via --wasm-no-tail-call).
};

/// Compiles a script to WebAssembly instead of executing it.
///
/// Reads the script, compiles it to CoreVM IR (no Shell, no execution), runs
/// the standard IR cleanup passes, and emits a WASI command module via the
/// WASM backend. Prints diagnostics to stderr.
///
/// @return the process exit code (EXIT_SUCCESS on success)
[[nodiscard]] int runCompileCommand(CompileOptions const& options);

} // namespace endo::compile
