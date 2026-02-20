// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <endo-language/builtins/BuiltinSignatures.hpp>

#include <CoreVM/CoreVM.hpp>

namespace endo
{

/// Registers the minimal runtime builtins needed for the parser.
/// This is shared by HoverProvider, DiagnosticsCollector, SymbolCollector, LspServer, etc.
/// Follows the TestRuntime pattern from TestHelper.cpp.
/// All callbacks are no-ops — signatures are registered solely for parser/IR validation.
/// @param runtime The CoreVM runtime to register builtins with
inline void registerStubRuntime(CoreVM::Runtime& runtime)
{
    // No-op resolver: all builtins get dummy callbacks that do nothing.
    auto noOpResolver = [](std::string_view, size_t) -> std::optional<CoreVM::NativeCallback::Functor> {
        return std::nullopt;
    };

    registerFSharpBuiltins(runtime, noOpResolver);
    registerShellBuiltins(runtime, noOpResolver);
    registerInternalBuiltins(runtime, noOpResolver);
    registerStructuredBuiltins(runtime, noOpResolver);
    registerPromptPropertyBuiltins(runtime, noOpResolver);
    registerAgentConfigPropertyBuiltins(runtime, noOpResolver);
}

} // namespace endo
