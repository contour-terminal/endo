// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <endo-language/codegen/IRGenerator.hpp>

#include <CoreVM/CoreVM.hpp>

#include <memory>
#include <string>
#include <string_view>

namespace endo
{

/// Creates a minimal FSharpPersistentState with the builtin structured
/// commands registered (ls, ps, ... producing record types).
[[nodiscard]] FSharpPersistentState makeDefaultPersistentState();

/// Compiles endo source text to CoreVM IR without executing it.
///
/// This is the shared one-shot compilation driver used by non-executing
/// consumers (the `endo -o` WASM compile path, endo-test's ir-only mode, unit
/// tests). It runs Parser and IRGenerator only — no Shell, no
/// TargetCodeGenerator, no Runner.
///
/// @param source               the script source (shebang already stripped)
/// @param runtime              a runtime with builtin signatures registered
///                             (a stub runtime is sufficient; see StubRuntime.hpp)
/// @param report               receives parse and IR-generation diagnostics
/// @param sourceName           name used in diagnostics (e.g. the file path)
/// @param unusedValueDetection enable unused-value analysis
/// @return the IR program, or nullptr if parsing or IR generation failed
///         (details in @p report)
[[nodiscard]] std::unique_ptr<CoreVM::IRProgram> compileToIR(std::string source,
                                                             CoreVM::Runtime& runtime,
                                                             CoreVM::diagnostics::Report& report,
                                                             std::string_view sourceName = {},
                                                             bool unusedValueDetection = false);

} // namespace endo
