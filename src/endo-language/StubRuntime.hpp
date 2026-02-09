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

    runtime.registerFunction("env.has")
        .param<CoreVM::CoreString>("key")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind([](CoreVM::Params& args) { args.setResult(false); });

    runtime.registerFunction("env.get")
        .param<CoreVM::CoreString>("key")
        .returnType(CoreVM::LiteralType::String)
        .bind([](CoreVM::Params& args) { args.setResult(""); });

    // Shell builtins required by Parser::parseBuiltinStatement()
    runtime.registerFunction("exit")
        .param<CoreVM::CoreNumber>("code")
        .returnType(CoreVM::LiteralType::Void)
        .bind(dummyHandler);

    runtime.registerFunction("true").returnType(CoreVM::LiteralType::Boolean).bind(dummyHandler);

    runtime.registerFunction("false").returnType(CoreVM::LiteralType::Boolean).bind(dummyHandler);

    runtime.registerFunction("export")
        .param<std::string>("name")
        .returnType(CoreVM::LiteralType::Void)
        .bind(dummyHandler);

    runtime.registerFunction("set")
        .param<std::string>("name")
        .param<std::string>("value")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind(dummyHandler);

    runtime.registerFunction("cd").returnType(CoreVM::LiteralType::Boolean).bind(dummyHandler);

    runtime.registerFunction("cd")
        .param<std::string>("path")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind(dummyHandler);

    runtime.registerFunction("unset")
        .param<std::string>("name")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind(dummyHandler);

    runtime.registerFunction("read").returnType(CoreVM::LiteralType::String).bind(dummyHandler);

    runtime.registerFunction("read")
        .param<std::vector<std::string>>("args")
        .returnType(CoreVM::LiteralType::String)
        .bind(dummyHandler);

    runtime.registerFunction("jobs").returnType(CoreVM::LiteralType::Number).bind(dummyHandler);

    runtime.registerFunction("fg").returnType(CoreVM::LiteralType::Number).bind(dummyHandler);

    runtime.registerFunction("fg")
        .param<CoreVM::CoreNumber>("job_id")
        .returnType(CoreVM::LiteralType::Number)
        .bind(dummyHandler);

    runtime.registerFunction("bg").returnType(CoreVM::LiteralType::Number).bind(dummyHandler);

    runtime.registerFunction("bg")
        .param<CoreVM::CoreNumber>("job_id")
        .returnType(CoreVM::LiteralType::Number)
        .bind(dummyHandler);

    runtime.registerFunction("wait").returnType(CoreVM::LiteralType::Number).bind(dummyHandler);

    runtime.registerFunction("wait")
        .param<CoreVM::CoreNumber>("job_id")
        .returnType(CoreVM::LiteralType::Number)
        .bind(dummyHandler);

    runtime.registerFunction("bind").returnType(CoreVM::LiteralType::Number).bind(dummyHandler);

    runtime.registerFunction("bind")
        .param<std::vector<std::string>>("args")
        .returnType(CoreVM::LiteralType::Number)
        .bind(dummyHandler);

    runtime.registerFunction("which").returnType(CoreVM::LiteralType::Number).bind(dummyHandler);

    runtime.registerFunction("which")
        .param<std::vector<std::string>>("args")
        .returnType(CoreVM::LiteralType::Number)
        .bind(dummyHandler);

    runtime.registerFunction("string_repeat")
        .param<CoreVM::CoreString>("str")
        .param<CoreVM::CoreNumber>("count")
        .returnType(CoreVM::LiteralType::String)
        .bind(dummyHandler);
}

} // namespace endo
