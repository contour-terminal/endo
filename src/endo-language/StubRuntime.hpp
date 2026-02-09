// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <CoreVM/CoreVM.hpp>

namespace endo
{

/// Registers the minimal runtime builtins needed for the parser.
/// This is shared by HoverProvider, DiagnosticsCollector, SymbolCollector, LspServer, etc.
/// Follows the TestRuntime pattern from TestHelper.cpp.
/// @param runtime The CoreVM runtime to register builtins with
inline void registerStubRuntime(CoreVM::Runtime& runtime)
{
    auto dummyHandler = [](CoreVM::Params&) {
    };

    runtime.registerFunction("callproc")
        .param<std::vector<std::string>>("args")
        .returnType(CoreVM::LiteralType::Number)
        .bind(dummyHandler);

    runtime.registerFunction("callproc")
        .param<bool>("last_in_chain")
        .param<std::vector<std::string>>("args")
        .returnType(CoreVM::LiteralType::Number)
        .bind(dummyHandler);

    runtime.registerFunction("print")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::Void)
        .bind(dummyHandler);

    runtime.registerFunction("println")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::Void)
        .bind(dummyHandler);
}

} // namespace endo
