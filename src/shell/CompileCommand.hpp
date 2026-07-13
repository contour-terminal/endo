// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <expected>
#include <string>
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

/// Reads a script file and strips a leading shebang line.
/// Shared by the compile mode and script execution.
/// @return the script source, or an error message ("<path>: <reason>")
[[nodiscard]] std::expected<std::string, std::string> readScriptSource(std::string_view scriptPath);

/// Compiles a script to WebAssembly instead of executing it.
///
/// Reads the script, compiles it through the shared source-to-wasm pipeline
/// (no Shell, no execution; see endo-language/CompileToWasm.hpp), and writes
/// a WASI command module. Prints diagnostics to stderr.
///
/// @return the process exit code (EXIT_SUCCESS on success)
[[nodiscard]] int runCompileCommand(CompileOptions const& options);

} // namespace endo::compile
