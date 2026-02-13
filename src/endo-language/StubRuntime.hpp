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

    runtime.registerFunction("setvar.exitstatus")
        .param<CoreVM::CoreNumber>("code")
        .returnType(CoreVM::LiteralType::Void)
        .bind(dummyHandler);

    runtime.registerFunction("list_to_string")
        .param<CoreVM::CoreNumber>("obj")
        .returnType(CoreVM::LiteralType::String)
        .bind(dummyHandler);

    runtime.registerFunction("object_to_string")
        .param<CoreVM::CoreNumber>("obj")
        .returnType(CoreVM::LiteralType::String)
        .bind(dummyHandler);

    runtime.registerFunction("list_concat")
        .param<CoreVM::CoreNumber>("left")
        .param<CoreVM::CoreNumber>("right")
        .returnType(CoreVM::LiteralType::Number)
        .bind(dummyHandler);

    runtime.registerFunction("list_head")
        .param<CoreVM::CoreNumber>("list")
        .returnType(CoreVM::LiteralType::Number)
        .bind(dummyHandler);

    runtime.registerFunction("list_tail")
        .param<CoreVM::CoreNumber>("list")
        .returnType(CoreVM::LiteralType::Number)
        .bind(dummyHandler);

    runtime.registerFunction("list_length")
        .param<CoreVM::CoreNumber>("list")
        .returnType(CoreVM::LiteralType::Number)
        .bind(dummyHandler);

    runtime.registerFunction("list_isEmpty")
        .param<CoreVM::CoreNumber>("list")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind(dummyHandler);

    runtime.registerFunction("list_sort")
        .param<CoreVM::CoreNumber>("list")
        .returnType(CoreVM::LiteralType::Number)
        .bind(dummyHandler);

    runtime.registerFunction("list_distinct")
        .param<CoreVM::CoreNumber>("list")
        .returnType(CoreVM::LiteralType::Number)
        .bind(dummyHandler);

    runtime.registerFunction("list_sort_pairs")
        .param<CoreVM::CoreNumber>("pairs")
        .returnType(CoreVM::LiteralType::Number)
        .bind(dummyHandler);

    runtime.registerFunction("list_group_pairs")
        .param<CoreVM::CoreNumber>("pairs")
        .returnType(CoreVM::LiteralType::Number)
        .bind(dummyHandler);

    runtime.registerFunction("string_repeat")
        .param<CoreVM::CoreString>("str")
        .param<CoreVM::CoreNumber>("count")
        .returnType(CoreVM::LiteralType::String)
        .bind(dummyHandler);

    // structured_ls builtin stub
    runtime.registerFunction("structured_ls")
        .param<CoreVM::CoreString>("path")
        .returnType(CoreVM::LiteralType::Number)
        .bind(dummyHandler);

    // structured_jobs builtin stub
    runtime.registerFunction("structured_jobs").returnType(CoreVM::LiteralType::Number).bind(dummyHandler);

    // Helper builtins for FileInfo mode/mtime formatting and testing
    runtime.registerFunction("format_datetime")
        .param<CoreVM::CoreNumber>("epoch")
        .returnType(CoreVM::LiteralType::String)
        .bind(dummyHandler);
    runtime.registerFunction("format_mode")
        .param<CoreVM::CoreNumber>("mode")
        .returnType(CoreVM::LiteralType::String)
        .bind(dummyHandler);
    runtime.registerFunction("mode_isReadable")
        .param<CoreVM::CoreNumber>("mode")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind(dummyHandler);
    runtime.registerFunction("mode_isWritable")
        .param<CoreVM::CoreNumber>("mode")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind(dummyHandler);
    runtime.registerFunction("mode_isExecutable")
        .param<CoreVM::CoreNumber>("mode")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind(dummyHandler);

    // Data source wrapper stubs (open-json, open-csv, from-json, from-csv)
    runtime.registerFunction("open_json")
        .param<CoreVM::CoreString>("path")
        .param<CoreVM::CoreString>("schema_desc")
        .param<CoreVM::CoreNumber>("type_id")
        .returnType(CoreVM::LiteralType::Number)
        .bind(dummyHandler);
    runtime.registerFunction("open_csv")
        .param<CoreVM::CoreString>("path")
        .param<CoreVM::CoreString>("schema_desc")
        .param<CoreVM::CoreNumber>("type_id")
        .returnType(CoreVM::LiteralType::Number)
        .bind(dummyHandler);
    runtime.registerFunction("from_json")
        .param<CoreVM::CoreString>("source_cmd")
        .param<CoreVM::CoreString>("schema_desc")
        .param<CoreVM::CoreNumber>("type_id")
        .returnType(CoreVM::LiteralType::Number)
        .bind(dummyHandler);
    runtime.registerFunction("from_csv")
        .param<CoreVM::CoreString>("source_cmd")
        .param<CoreVM::CoreString>("schema_desc")
        .param<CoreVM::CoreNumber>("type_id")
        .returnType(CoreVM::LiteralType::Number)
        .bind(dummyHandler);

    // Output definition structured command stubs
    runtime.registerFunction("structured_docker_ps")
        .returnType(CoreVM::LiteralType::Number)
        .bind(dummyHandler);
    runtime.registerFunction("structured_docker_images")
        .returnType(CoreVM::LiteralType::Number)
        .bind(dummyHandler);
    runtime.registerFunction("structured_git_log").returnType(CoreVM::LiteralType::Number).bind(dummyHandler);
    runtime.registerFunction("structured_git_status")
        .returnType(CoreVM::LiteralType::Number)
        .bind(dummyHandler);

    // Command substitution builtins (needed for structured pipeline fallback)
    runtime.registerFunction("internal.subst_start").returnType(CoreVM::LiteralType::Void).bind(dummyHandler);
    runtime.registerFunction("internal.subst_end").returnType(CoreVM::LiteralType::String).bind(dummyHandler);
}

} // namespace endo
